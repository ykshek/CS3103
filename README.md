This program simulates a image compression pipeline with three threads:

Camera Thread: Generates frames and loads them into cache

Transformer Thread: Compresses frames using quantization/dequantization

Estimator Thread: Calculates Mean Squared Error (MSE) between original and compressed frames

Compile:
# 1. Compile C files to object files
gcc -c generate_frame_vector.c -o generate_frame_vector.o
gcc -c compression.c -o compression.o -lm

# 2. Compile C++ file to object file  
g++ -c 58532418_58533440_58542922.cpp -o main.o

# 3. Link all object files
g++ main.o generate_frame_vector.o compression.o -lpthread -lm -o CS3103
