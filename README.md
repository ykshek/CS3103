This program simulates a image compression pipeline with three threads:

Camera Thread: Generates frames and loads them into cache

Transformer Thread: Compresses frames using quantization/dequantization

Estimator Thread: Calculates Mean Squared Error (MSE) between original and compressed frames
