# Multi-threaded Image Compression Simulation
A program simulating image compression demonstrating the use of multi-threading, mutexes, and semaphores.

## Overview
This program simulates an image compression pipeline with three threads:
- Camera Thread: Generates frames and loads them into cache
- Transformer Thread: Compresses frames using quantization/dequantization
- Estimator Thread: Calculates Mean Squared Error (MSE) between original and compressed frames

## Build Instructions
Platform: Fedora 42 or Ubuntu 24.04.2 LTS

Compiler: g++ 4.8+ with `std=c++11` flag

1. Clone the project or unzip the included archive :
```bash
git clone https://github.com/ykshek/CS3103.git
```
2. Go to the working directory
```bash
cd CS3103
```
3. Compile the project with the makefile or manually with the appropriate flags
```bash
make
```
This compiles all source files and produces the executable: 58532418_58533440_58542922
	

## Usage

Run the program by 
```bash
./58532418_58533440_58542922 [INTERVAL] [THREADS] [OPTIONS]
```
Arguments:

- INTERVAL: Integer, seconds between frame generation (default: 2).

- THREADS: Integer, number of camera threads (default: 4).

Options:

- -h, --help: Show help message.

- -v, --verbose: Enable verbose logging.

### Exit Status:

-0   if OK

-1   if problem occurs

## Example
```bash
./58532418_58533440_58542922 2
```
## Expected Output
	
	mse = 0.000541
	mse = 0.000578
	mse = 0.001112
	...
