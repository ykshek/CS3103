#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <cstring>
#include <atomic>

using namespace std;

#define CACHE_SIZE 5
#define FRAME_LEN 8
#define DEFAULT_THREADS 2
#define DEFAULT_INTERVAL_SECONDS 2
#define COMPRESS_TIME 3

// Prototype function declarations
double* generate_frame_vector(int length);
double* compression(double* frame, int length);

//Michael-Scott Non-blocking Queue algorithm
struct Node {
    double* frame;
    atomic<Node*> next;
    
    Node(double* f = nullptr) : frame(f), next(nullptr) {}
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
        while (dequeue() != nullptr) {}
        Node* dummy = head.load();
        if (dummy) delete dummy;
    }
    
    bool enqueue(double* frame) {
        Node* node = new Node(frame);
        Node* last, *next;
        
        while (true) {
            last = tail.load(memory_order_acquire);
            next = last->next.load(memory_order_acquire);
            
            if (last == tail.load(memory_order_acquire)) {
                if (next == nullptr) {
                    if (last->next.compare_exchange_weak(next, node, memory_order_release)) {
                        tail.compare_exchange_strong(last, node, memory_order_release);
                        return true;
                    }
                } else {
                    tail.compare_exchange_strong(last, next, memory_order_release);
                }
            }
        }


//===================Initialize all data types===================
struct QueueEntry {
    double* frame;
};
struct Queue {
    QueueEntry queue[CACHE_SIZE];
    int front, rear, count;

    bool full() {return count == CACHE_SIZE;}
    bool empty() {return count == 0;}

    bool enqueue(double* frame) {
        if (full()) return false;
        queue[rear].frame = frame;
        rear = (rear + 1) % CACHE_SIZE;
        count++;
        return true;

    }
    
    double* dequeue() {

        Node* first, *last, *next;
        double* result;
        
        while (true) {
            first = head.load(memory_order_acquire);
            last = tail.load(memory_order_acquire);
            next = first->next.load(memory_order_acquire);
            
            if (first == head.load(memory_order_acquire)) {
                if (first == last) {
                    if (next == nullptr) {
                        return nullptr;
                    }
                    tail.compare_exchange_strong(last, next, memory_order_release);
                } else {
                    result = next->frame;
                    if (head.compare_exchange_weak(first, next, memory_order_release)) {
                        delete first;
                        return result;
                    }
                }
            }
        }

        if (empty()) return NULL;
        double* output = queue[front].frame;
        front = (front + 1) % CACHE_SIZE;
        count--;
        return output;

    }
    
    double* get_noDequeue() {

        Node* first = head.load(memory_order_acquire);
        Node* next = first->next.load(memory_order_acquire);
        return (next != nullptr) ? next->frame : nullptr;
    }
    
    bool empty() {
        Node* first = head.load(memory_order_acquire);
        Node* next = first->next.load(memory_order_acquire);
        return next == nullptr;
    }
    
    bool full() {
        // Flow control handled by semaphores
        return false;

        if (empty()) return NULL;
        return queue[front].frame;

    }
};

double calculate_mse(const double* a, const double* b, int len) {
    double mse = 0.0;
    for (int i = 0; i < len; ++i)
        mse += (a[i] - b[i]) * (a[i] - b[i]);
    mse /= len;
    return mse;
}



struct thread_args {
    int interval;
};

//===================All Semaphores and Mutexes===================
Queue frame_cache;
sem_t cache_emptied;
sem_t cache_loaded; // ready for transformer
sem_t transformer_loaded; // ready for MSE
sem_t mse_loaded; // ready for camera
sem_t framebuffer_clear;
double temp[FRAME_LEN]; // "temporary frame recorder"

pthread_mutex_t mutex, framebuffer_mutex = PTHREAD_MUTEX_INITIALIZER;



struct thread_args {
    int interval;
};


MSQueue frame_cache;
sem_t cache_emptied;
sem_t cache_loaded;
sem_t transformer_loaded;
sem_t mse_loaded;
sem_t framebuffer_clear;
double temp[FRAME_LEN];

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t framebuffer_mutex = PTHREAD_MUTEX_INITIALIZER;


void* camera(void* input) {
    struct thread_args *x = (struct thread_args *)input;
    int INTERVAL_SECONDS = (x->interval);

    while (true) {
        if (frame_cache.full()) sem_wait(&cache_emptied);


        double* frame = generate_frame_vector(FRAME_LEN);
        if (frame != NULL) {
            frame_cache.enqueue(frame);
            sleep(INTERVAL_SECONDS);
        }
        sem_post(&cache_loaded);
        if (!frame) break;

        // ^^ Wait for signal after buffer is full

        pthread_mutex_lock(&mutex);
        double* frame = NULL;
        if (!frame_cache.full())
            frame = generate_frame_vector(FRAME_LEN);
        if (frame != NULL) {
            frame_cache.enqueue(frame);
            sleep(INTERVAL_SECONDS);
            // ^^ "Take interval seconds to load a frame into the cache
        }
        pthread_mutex_unlock(&mutex);
        sem_post(&cache_loaded); // Notify frame is available
        if (!frame) break;
        // ^^ Exit after NULL from generate_frame_vector

    }
    pthread_exit(NULL);
}

void* transformer(void* args) {
    while (!frame_cache.empty()) {
        sem_wait(&framebuffer_clear);
        sem_wait(&cache_loaded);

        double* original = frame_cache.get_noDequeue();
        if (original == NULL) continue;



    while (!frame_cache.empty()) {
        sem_wait(&framebuffer_clear); // initially 1 to represent clear
        sem_wait(&cache_loaded);

        // CS1: get from cache
        pthread_mutex_lock(&mutex);
        double* original = nullptr;
        original = frame_cache.get_noDequeue(); // Dangerous!
        pthread_mutex_unlock(&mutex);
        if (original == NULL) continue;

        // CS2: put in framebuffer
        pthread_mutex_lock(&framebuffer_mutex);
        memcpy(temp, original, FRAME_LEN * sizeof(double));
        compression(temp, FRAME_LEN);
        sleep(COMPRESS_TIME);

        pthread_mutex_unlock(&framebuffer_mutex);

        sem_post(&transformer_loaded);

        // ^^ take 3 seconds to compress the extracted frame
        pthread_mutex_unlock(&framebuffer_mutex);

        sem_post(&transformer_loaded);
        // ^^ Signals estimator to compute the MSE
    }
    pthread_exit(NULL);
}

void* estimator(void* args) {
    while (!frame_cache.empty()) {
        sem_wait(&transformer_loaded);

        double* original = frame_cache.dequeue();
        double* compressed = temp;
        double mse = calculate_mse(original, compressed, FRAME_LEN);

        printf("mse = %f\n", mse);
        free(original); 
        

    while (!frame_cache.empty()) {
        sem_wait(&transformer_loaded);
        // framebuffer is empty once we got this semaphore
        // lock both mutexes
        pthread_mutex_lock(&mutex);
        pthread_mutex_lock(&framebuffer_mutex);
        double* original = frame_cache.dequeue();
        double* compressed = temp;
        double mse = calculate_mse(original, compressed, FRAME_LEN);
        // ^^ Compare cache and framebuffer's mse
        pthread_mutex_unlock(&mutex);
        pthread_mutex_unlock(&framebuffer_mutex);

        printf("mse: %f\n", mse);
        sem_post(&cache_emptied);
        sem_post(&framebuffer_clear);
    }
    pthread_exit(NULL);
}


//===================Main Program===================
int main(int argc, char *argv[]) {
    int i, rc, INTERVAL_SECONDS, THREADS;
    if (argc > 3) {
        printf("Invalid number of arguments.");
        return 0;
    }

    if (argc >= 2) INTERVAL_SECONDS = atoi(argv[1]);
    else INTERVAL_SECONDS = DEFAULT_INTERVAL_SECONDS;
    if (argc >= 3) THREADS = atoi(argv[2]);
    else THREADS = DEFAULT_THREADS;

    sem_init(&cache_loaded, 0, 0);
    sem_init(&cache_emptied, 0, 0);
    sem_init(&transformer_loaded, 0, 0);
    sem_init(&mse_loaded, 0, 0);
    sem_init(&framebuffer_clear, 0, 1);

    pthread_t camera_thread, transformer_thread, estimator_thread;

    struct thread_args *x = (struct thread_args *)malloc(sizeof(struct thread_args));
    x->interval = INTERVAL_SECONDS;

    for (i = 0; i < THREADS; i++) {
        rc = pthread_create(&camera_thread, NULL, camera, (void *)x);
        if (rc) {
            cout << "Error when creating camera threads!" << endl;
            exit(-1);
        }
    }
    pthread_create(&transformer_thread, NULL, transformer, NULL);
    pthread_create(&estimator_thread, NULL, estimator, NULL);

    pthread_join(camera_thread, NULL);
    pthread_join(transformer_thread, NULL);
    pthread_join(estimator_thread, NULL);


    sem_destroy(&cache_loaded);
    sem_destroy(&cache_emptied);
    sem_destroy(&transformer_loaded);
    sem_destroy(&mse_loaded);
    sem_destroy(&framebuffer_clear);

    pthread_mutex_destroy(&mutex);
    pthread_mutex_destroy(&framebuffer_mutex);

    return 0;
}
