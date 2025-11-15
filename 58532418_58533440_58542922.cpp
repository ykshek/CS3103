#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <cstring>
#include <atomic>
#include <random>

using namespace std;

#define CACHE_SIZE 5
#define FRAME_LEN 8
#define DEFAULT_THREADS 2
#define DEFAULT_INTERVAL_SECONDS 2
#define COMPRESS_TIME 3

// Prototype function declarations
double* generate_frame_vector(int length);
double* compression(double* frame, int length);

//===================Michael-Scott Non-blocking Queue===================
struct Node {
    double* frame;
    atomic<Node*> next;
    
    Node(double* f = nullptr) : frame(f), next(nullptr) {}
    ~Node() {
        // Don't delete frame here - it's managed by the application
    }
};

struct MSQueue {
    atomic<Node*> head;
    atomic<Node*> tail;
    
    MSQueue() {
        Node* dummy = new Node();
        head.store(dummy);
        tail.store(dummy);
    }
    
    ~MSQueue() {
        // Basic cleanup - in production you'd need more sophisticated memory management
        Node* curr = head.load();
        while (curr) {
            Node* next = curr->next.load();
            delete curr;
            curr = next;
        }
    }
    
    bool enqueue(double* frame) {
        Node* node = new Node(frame);
        Node* last, *next;
        
        while (true) {
            last = tail.load(memory_order_acquire);
            next = last->next.load(memory_order_acquire);
            
            if (last == tail.load(memory_order_acquire)) {
                if (next == nullptr) {
                    if (last->next.compare_exchange_weak(next, node, memory_order_release, memory_order_relaxed)) {
                        tail.compare_exchange_strong(last, node, memory_order_release, memory_order_relaxed);
                        return true;
                    }
                } else {
                    tail.compare_exchange_strong(last, next, memory_order_release, memory_order_relaxed);
                }
            }
        }
    }
    
    double* dequeue() {
        Node* first, *last, *next;
        double* result = nullptr;
        
        while (true) {
            first = head.load(memory_order_acquire);
            last = tail.load(memory_order_acquire);
            next = first->next.load(memory_order_acquire);
            
            if (first == head.load(memory_order_acquire)) {
                if (first == last) {
                    if (next == nullptr) {
                        return nullptr; // Queue is empty
                    }
                    tail.compare_exchange_strong(last, next, memory_order_release, memory_order_relaxed);
                } else {
                    if (next == nullptr) continue; // Safety check
                    result = next->frame;
                    if (head.compare_exchange_weak(first, next, memory_order_release, memory_order_relaxed)) {
                        delete first; // Free old dummy node
                        return result;
                    }
                }
            }
        }
    }
    
    double* peek() {
        Node* first = head.load(memory_order_acquire);
        Node* next = first->next.load(memory_order_acquire);
        
        if (next == nullptr) {
            return nullptr;
        }
        return next->frame;
    }
    
    bool empty() {
        Node* first = head.load(memory_order_acquire);
        Node* next = first->next.load(memory_order_acquire);
        return next == nullptr;
    }
};

double calculate_mse(const double* a, const double* b, int len) {
    double mse = 0.0;
    for (int i = 0; i < len; ++i) {
        double diff = a[i] - b[i];
        mse += diff * diff;
    }
    mse /= len;
    return mse;
}

struct thread_args {
    int interval;
};

//===================All Semaphores and Shared Data===================
MSQueue frame_cache;
atomic<int> cache_count{0};
const int MAX_CACHE_SIZE = CACHE_SIZE;

sem_t cache_emptied;
sem_t cache_loaded;
sem_t transformer_loaded;
sem_t framebuffer_clear;
double temp[FRAME_LEN];

pthread_mutex_t framebuffer_mutex = PTHREAD_MUTEX_INITIALIZER;
atomic<bool> program_running{true};

//===================All Thread Declarations===================
void* camera(void* input) {
    struct thread_args *x = (struct thread_args *)input;
    int INTERVAL_SECONDS = x->interval;

    while (program_running.load(memory_order_acquire)) {
        // Wait if cache is full
        while (cache_count.load(memory_order_acquire) >= MAX_CACHE_SIZE && 
               program_running.load(memory_order_acquire)) {
            sem_wait(&cache_emptied);
        }
        
        if (!program_running.load(memory_order_acquire)) break;

        double* frame = generate_frame_vector(FRAME_LEN);
        if (frame != NULL) {
            if (frame_cache.enqueue(frame)) {
                cache_count.fetch_add(1, memory_order_release);
                printf("Camera: Enqueued frame, cache count: %d\n", cache_count.load());
                sleep(INTERVAL_SECONDS);
                sem_post(&cache_loaded);
            } else {
                delete[] frame;
            }
        } else {
            break;
        }
    }
    printf("Camera thread exiting\n");
    pthread_exit(NULL);
}

void* transformer(void* args) {
    while (program_running.load(memory_order_acquire) || !frame_cache.empty()) {
        if (sem_trywait(&framebuffer_clear) != 0) continue;
        if (sem_trywait(&cache_loaded) != 0) {
            sem_post(&framebuffer_clear);
            if (!program_running.load(memory_order_acquire) && frame_cache.empty()) break;
            continue;
        }

        double* original = frame_cache.peek();
        if (original == NULL) {
            sem_post(&framebuffer_clear);
            continue;
        }

        pthread_mutex_lock(&framebuffer_mutex);
        memcpy(temp, original, FRAME_LEN * sizeof(double));
        compression(temp, FRAME_LEN);
        sleep(COMPRESS_TIME);
        pthread_mutex_unlock(&framebuffer_mutex);

        sem_post(&transformer_loaded);
    }
    printf("Transformer thread exiting\n");
    pthread_exit(NULL);
}

void* estimator(void* args) {
    while (program_running.load(memory_order_acquire) || !frame_cache.empty()) {
        if (sem_trywait(&transformer_loaded) != 0) {
            if (!program_running.load(memory_order_acquire) && frame_cache.empty()) break;
            continue;
        }
        
        double* original = frame_cache.dequeue();
        if (original != NULL) {
            cache_count.fetch_sub(1, memory_order_release);
            
            pthread_mutex_lock(&framebuffer_mutex);
            double mse = calculate_mse(original, temp, FRAME_LEN);
            pthread_mutex_unlock(&framebuffer_mutex);
            
            printf("Estimator: MSE: %f, cache count: %d\n", mse, cache_count.load());
            delete[] original;
            
            sem_post(&cache_emptied);
            sem_post(&framebuffer_clear);
        } else {
            sem_post(&framebuffer_clear);
        }
    }
    printf("Estimator thread exiting\n");
    pthread_exit(NULL);
}

//===================Missing Function Implementations===================
double* generate_frame_vector(int length) {
    // Stop after generating some frames for demonstration
    static int frame_count = 0;
    if (frame_count++ > 10) {
        program_running.store(false, memory_order_release);
        // Post to all semaphores to wake up waiting threads
        sem_post(&cache_loaded);
        sem_post(&transformer_loaded);
        sem_post(&cache_emptied);
        sem_post(&framebuffer_clear);
        return nullptr;
    }
    
    double* frame = new double[length];
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> dis(0.0, 1.0);
    
    for (int i = 0; i < length; i++) {
        frame[i] = dis(gen);
    }
    return frame;
}

double* compression(double* frame, int length) {
    // Simple compression simulation
    for (int i = 0; i < length; i++) {
        frame[i] *= 0.9; // Reduce by 10%
    }
    return frame;
}

//===================Main Program===================
int main(int argc, char *argv[]) {
    int i, rc, INTERVAL_SECONDS, THREADS;
    
    if (argc > 3) {
        printf("Usage: %s [interval_seconds] [thread_count]\n", argv[0]);
        return 1;
    }

    INTERVAL_SECONDS = (argc >= 2) ? atoi(argv[1]) : DEFAULT_INTERVAL_SECONDS;
    THREADS = (argc >= 3) ? atoi(argv[2]) : DEFAULT_THREADS;

    printf("Starting frame processor with %d threads, %d second interval\n", 
           THREADS, INTERVAL_SECONDS);

    // Initialize semaphores
    sem_init(&cache_loaded, 0, 0);
    sem_init(&cache_emptied, 0, 0);
    sem_init(&transformer_loaded, 0, 0);
    sem_init(&framebuffer_clear, 0, 1);

    pthread_t camera_threads[THREADS];
    pthread_t transformer_thread, estimator_thread;

    struct thread_args args;
    args.interval = INTERVAL_SECONDS;

    // Create camera threads
    for (i = 0; i < THREADS; i++) {
        rc = pthread_create(&camera_threads[i], NULL, camera, (void *)&args);
        if (rc) {
            cerr << "Error creating camera thread " << i << endl;
            exit(1);
        }
    }
    
    // Create transformer and estimator threads
    pthread_create(&transformer_thread, NULL, transformer, NULL);
    pthread_create(&estimator_thread, NULL, estimator, NULL);

    // Wait for all threads to complete
    for (i = 0; i < THREADS; i++) {
        pthread_join(camera_threads[i], NULL);
    }
    pthread_join(transformer_thread, NULL);
    pthread_join(estimator_thread, NULL);

    // Cleanup
    sem_destroy(&cache_loaded);
    sem_destroy(&cache_emptied);
    sem_destroy(&transformer_loaded);
    sem_destroy(&framebuffer_clear);
    pthread_mutex_destroy(&framebuffer_mutex);

    printf("Program completed successfully\n");
    return 0;
}
