#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <cstring>
#include <getopt.h>
#include "verbose.h"
#include "MSqueue.c"

using namespace std;

#define CACHE_SIZE 5
#define FRAME_LEN 8
#define DEFAULT_THREADS 4
#define DEFAULT_INTERVAL_SECONDS 2
#define COMPRESS_TIME 3

//===================Prototype function and function declarations===================
double* generate_frame_vector(int length);
double* compression(double* frame, int length);

double calculate_mse(const double* a, const double* b, int len)
{
    double mse = 0.0;
    for (int i = 0; i < len; ++i)
        mse += (a[i] - b[i]) * (a[i] - b[i]);
    mse /= len;
    return mse;
}

struct thread_args
{
    int interval;
};

//===================Initialize Semaphores and Mutexes===================
MSQueue frame_cache;
sem_t cache_emptied;
sem_t cache_loaded; // ready for transformer
sem_t transformer_loaded; // ready for MSE
sem_t mse_loaded; // ready for camera
sem_t framebuffer_clear;
double temp[FRAME_LEN]; // "temporary frame recorder"

pthread_mutex_t generator_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t framebuffer_mutex = PTHREAD_MUTEX_INITIALIZER;

//===================Thread Functions=====================
void* camera(void* input)
{
    struct thread_args *x = (struct thread_args *)input;
    int INTERVAL_SECONDS = (x->interval);

    while (true)
    {
        if (frame_cache.full())
            sem_wait(&cache_emptied);
        pthread_mutex_lock(&generator_mutex);
        double* frame = generate_frame_vector(FRAME_LEN);   // lock the generator each time it is used
        pthread_mutex_unlock(&generator_mutex);             // so that order of generation is maintained
        if (frame != NULL)
        {
            frame_cache.enqueue(frame);
            sleep(INTERVAL_SECONDS);        // Simulate time to load a frame into the cache
        }
        sem_post(&cache_loaded);            // Notify frame is available
        if (!frame) break;                  // Exit after NULL from generate_frame_vector
        verbose("\t\t\tCamera Looping\n");  // ^break must be used here as frame is a local variable
    }
    verbose("\t\t\tCamera Exits\n");
    pthread_exit(NULL);
}

void* transformer(void* args)
{
    do
    {
        sem_wait(&framebuffer_clear); // initially 1 to represent clear
        sem_wait(&cache_loaded);

        // CS1: get from cache (protected by M-S queue, therefore no need for mutexes.)
        double* original = nullptr;
        original = frame_cache.get_noDequeue();
        if (original == NULL) continue;

        // CS2: put into framebuffer (Protected by framebuffer_mutex)
        pthread_mutex_lock(&framebuffer_mutex);
        memcpy(temp, original, FRAME_LEN * sizeof(double));
        compression(temp, FRAME_LEN);
        sleep(COMPRESS_TIME);               // Simulate time to compress the extracted frame
        pthread_mutex_unlock(&framebuffer_mutex);

        sem_post(&transformer_loaded);      // Signals estimator to compute the MSE
        verbose("\t\t\t\t\tTransformer Looping\n");
    } while (!frame_cache.empty());
    // Use post-test loop to avoid getting stuck on exit
    verbose("\t\t\t\t\tTransformer Exits\n");
    pthread_exit(NULL);
}

void* estimator(void* args)
{
    do
    {
        sem_wait(&transformer_loaded);
        // framebuffer is empty once we get this semaphore

        double* original = frame_cache.dequeue();
        double* compressed = temp;
        double mse = calculate_mse(original, compressed, FRAME_LEN);
        printf("mse = %f\n", mse);
        
        sem_post(&cache_emptied);
        sem_post(&framebuffer_clear);
        verbose("\t\t\t\t\t\t\t\tEstimator Looping\n");
    } while (!frame_cache.empty());
    // Use post-test loop to avoid getting stuck on exit
    verbose("\t\t\t\t\t\t\t\tEstimator Exits\n");
    pthread_exit(NULL);
}

void usage(void)
{
    printf( "Usage: 58532418_58533440_58542922 [INTERVAL] [THREADS] [OPTION]...\n"
            "\t-h, --help\t\t display this help\n"
            "\t-v, --verbose\t\t show entered values\n"
            "\n"
            "The INTERVAL argument is an integer, specifying seconds to sleep.\n"
            "If unset or invalid, defaults to 2 seconds.\n"
            "\n"
            "The THREADS argument is an integer, specifying number of camera thread(s) spawned.\n"
            "If unset or invalid, defaults to 4 threads.\n"
            "\n"
            "Exit status:\n"
            "0\tif OK,\n"
            "1\tif problem occurs.\n"
    );
    exit(0);
}

//===================Main Program===================
int main(int argc, char **argv)
{
    int i, rc, INTERVAL_SECONDS, THREADS;
    INTERVAL_SECONDS = DEFAULT_INTERVAL_SECONDS;
    THREADS = DEFAULT_THREADS;
    int opt;
    const char *options = "hv";
    struct option long_options[] =
    {   // *name,   has_arg,        *flag,  val
        {"help",    no_argument,    NULL,   'h'},
        {"verbose", no_argument,    NULL,   'v'},
        {NULL,      0,              NULL,   0}
    };

    while (true)
    {
        opt = getopt_long(argc, argv, options, long_options, NULL);
        if (opt == -1)
            break;
        switch(opt)
        {
            case 'h':
                usage();
                break;
            case 'v':
                setVerbose(true);
                break;
            case '?':
                printf("unknown option '%c' (decimal: %d)\n", optopt, optopt);
                usage();
                break;
            default:
                printf("Error at '%c' (decimal: %d)\n", optopt, optopt);
                exit(1);
                break;
        }
        verbose("looping at '%c' (decimal: %d)\n", optopt, optopt);
    }
    int nargs = argc - optind?optind:1;
    verbose("nargs = %d\n", nargs);
    verbose("argv[nargs]: %s\n", argv[nargs]);
    verbose("argv[nargs+1]: %s\n", argv[nargs+1]);

    // Cstring to int conversion
    // fallback to default values if invalid
    char *endptr;
    int interval, threads = 0;
    if (argv[nargs])
        interval = strtol(argv[nargs], &endptr, 10);
    INTERVAL_SECONDS = (*endptr != '\0' || !interval)? DEFAULT_INTERVAL_SECONDS: interval;
    if (argv[nargs + 1])
        threads = strtol(argv[nargs+1], &endptr, 10);
    THREADS = (*endptr != '\0' || !threads)? DEFAULT_THREADS: threads;

    verbose("Running simulation with parameters:\n");
    verbose("INTERVAL: %d\n", INTERVAL_SECONDS);
    verbose("THREADS: %d\n", THREADS);

    sem_init(&cache_loaded, 0, 0);
    sem_init(&cache_emptied, 0, 0);
    sem_init(&transformer_loaded, 0, 0);
    sem_init(&mse_loaded, 0, 0);
    sem_init(&framebuffer_clear, 0, 1); // initially 1 to represent clear

    pthread_t camera_thread, transformer_thread, estimator_thread;

    struct thread_args *x = (struct thread_args *)malloc(sizeof(struct thread_args));
    x->interval = INTERVAL_SECONDS;

    for (i = 0; i < THREADS; i++) {
        rc = pthread_create(&camera_thread, NULL, camera, (void *)x);
        if (rc) {
            cout << "Error when creating camera threads!" << endl;
            exit(1);
        }
    }
    pthread_create(&transformer_thread, NULL, transformer, NULL);
    pthread_create(&estimator_thread, NULL, estimator, NULL);
    /* Only 1 of transformer_thread and estimator_thread,
     * because there is only 1 framebuffer slot,
     * and each operation is _always_ blocking,
     * and thus pointless to have multiple threads.
     */

    pthread_join(camera_thread, NULL);
    pthread_join(transformer_thread, NULL);
    pthread_join(estimator_thread, NULL);

    sem_destroy(&cache_loaded);
    sem_destroy(&cache_emptied);
    sem_destroy(&transformer_loaded);
    sem_destroy(&mse_loaded);
    sem_destroy(&framebuffer_clear);

    pthread_mutex_destroy(&generator_mutex);
    pthread_mutex_destroy(&framebuffer_mutex);

    return 0;
}
