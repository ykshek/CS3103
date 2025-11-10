#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include "generate_frame_vector.c"
#include "compression.c"
#include <cstring>

using namespace std;

// given in document: FIFO buffer size = 5, generate_frame_vector(l), where l = 8
#define CACHE_SIZE 5
#define FRAME_LEN 8

struct QueueEntry {
    double* original_frame;
    double* compressed_frame;
};

struct Queue {
    QueueEntry queue[CACHE_SIZE];
    int front, rear, count;

    bool full() {return count == CACHE_SIZE;}
    bool empty() {return count == 0;}

    bool enqueue(double* frame) {
        if (full()) return false;
        queue[rear].original_frame = frame;
        queue[rear].compressed_frame = NULL;
        rear = (rear + 1) % CACHE_SIZE;
        count++;
        return true;
    }
    double* dequeue() {
        if (empty()) return NULL;
        // id = front;
        double* output = queue[front].original_frame;
        front = (front + 1) % CACHE_SIZE;
        count--;
        return output;
    }
    double* get_noDequeue() {
        if (empty()) return NULL;
        return queue[front].original_frame;
    }
    double* set_compressed(double* compressed) {
        if (!empty()) queue[front].compressed_frame = compressed;
    }
};

Queue frame_cache;

sem_t cache_emptied;
sem_t cache_loaded; // ready for transformer
sem_t transformer_loaded; // ready for MSE
sem_t mse_loaded; // ready for camera
double temp[FRAME_LEN]; // "temporary frame recorder"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

double calculate_mse(const double* a, const double* b, int len) {
    double mse = 0.0;
    for (int i = 0; i < len; ++i)
        mse += (a[i] - b[i]) * (a[i] - b[i]);
    mse /= len;
    return mse;
}


void* camera(void* arg) {
    int INTERVAL_SECONDS = (int)(intptr_t)arg;
    int done = 0;
    while(!done) {
        double* frame = generate_frame_vector(FRAME_LEN);
        if (!frame) break;
        // ^^ "After a certain number of frames are generated, the function generate_frame_vector() returns NULL and the camera exits."
        if (frame_cache.full()) sem_wait(&cache_emptied);
        // ^^ "When the cache is full, the camera has to wait for the signal from the estimator after it deletes a frame from the cache."
        pthread_mutex_lock(&mutex);
        frame_cache.enqueue(frame);
        sleep(INTERVAL_SECONDS);
        // ^^ "Takes the camera interval seconds to load a frame into the cache."
        pthread_mutex_unlock(&mutex);

        sem_post(&cache_loaded);
    }
    return NULL;
}

void* transformer(void* arg) {
    while (true) {
        sem_wait(&cache_loaded);
        pthread_mutex_lock(&mutex);
        if (frame_cache.empty()) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        double* original = frame_cache.get_noDequeue();
        memcpy(temp, original, FRAME_LEN * sizeof(double));
        // ^^ "In the temporary frame recorder"
        frame_cache.set_compressed(compression(temp, FRAME_LEN));
        sleep(3);
        // ^^ "It takes the transformer 3 seconds to compress the extracted frame..."
        pthread_mutex_unlock(&mutex);
        sem_post(&transformer_loaded);
        // ^^ "Signals the estimator to computer the MSE."
    }
    return NULL;
}

void* estimator(void* arg) {
    while (true) {
        sem_wait(&transformer_loaded);
        pthread_mutex_lock(&mutex);
        if (frame_cache.empty()) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        double* original = frame_cache.get_noDequeue();
        double* compressed = temp;
        // ^^ "...between the compressed frame in the temporary frame recorder and the corresponding original frame in the cache."
        double mse = calculate_mse(original, compressed, FRAME_LEN);
        printf("MSE: %f\n", mse);

        frame_cache.dequeue();
        pthread_mutex_unlock(&mutex);
        sem_post(&cache_emptied);
    }
    return NULL;
}

int main(int argc, char *argv[]) {

    if (argc != 2) {
        printf("Invalid number of arguments.");
        return 0;
    }
    int INTERVAL_SECONDS = *argv[1] - '0';

    sem_init(&cache_loaded, 0, 0);
    sem_init(&cache_emptied, 0, CACHE_SIZE);
    sem_init(&transformer_loaded, 0, 0);
    sem_init(&mse_loaded, 0, 0);

    pthread_t camera_thread, transformer_thread, estimator_thread;

    pthread_create(&camera_thread, NULL, camera, (void *)(intptr_t)INTERVAL_SECONDS);
    pthread_create(&transformer_thread, NULL, transformer, NULL);
    pthread_create(&estimator_thread, NULL, estimator, NULL);

    pthread_join(camera_thread, NULL);
    pthread_join(transformer_thread, NULL);
    pthread_join(estimator_thread, NULL);

    sem_destroy(&cache_loaded);
    sem_destroy(&cache_emptied);
    sem_destroy(&transformer_loaded);
    sem_destroy(&mse_loaded);

    pthread_mutex_destroy(&mutex);

    return 0;
}





