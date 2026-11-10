#ifndef NEURAL_NETWORK_H
#define NEURAL_NETWORK_H

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

using namespace godot;

// ==================== NEURAL NETWORK CLASS ====================

class NeuralNet : public Node2D {
    GDCLASS(NeuralNet, Node2D)

protected:
    // ==================== NEURAL NETWORK FRAMEWORK ====================

    // Network architecture
    std::vector<int> layer_sizes;  // Size of each layer (input, hidden layers, output)
    std::vector<std::vector<std::vector<float>>> weights;  // weights[layer][neuron][input]
    std::vector<std::vector<float>> biases;  // biases[layer][neuron]
    std::vector<std::vector<float>> activations;  // activations[layer][neuron] (for forward pass)

    // Activation function per layer (for hidden layers only)
    // 0=linear, 1=relu, 2=sigmoid, 3=tanh
    std::vector<int> activation_functions;

    // Helper: Convert activation string to int
    int activation_string_to_int(const String &activation_str) const;

    // Helper: Convert activation int to string
    String activation_int_to_string(int activation_type) const;

    // Network initialized flag
    bool network_initialized;

    // ==================== TRAINING INFRASTRUCTURE ====================

    // Gradients for backpropagation (same structure as weights/biases)
    std::vector<std::vector<std::vector<float>>> weight_gradients;
    std::vector<std::vector<float>> bias_gradients;

    // Pre-activation values (z values before activation function)
    std::vector<std::vector<float>> z_values;

    // Delta values for backpropagation
    std::vector<std::vector<float>> deltas;

    // Forward pass through neural network with provided input features
    // Returns the network output value (between 0 and 1 via sigmoid)
    float forward_pass(const std::vector<float> &input_features);

    // Specialized forward pass functions for different activation types
    void forward_pass_linear(size_t layer_idx);
    void forward_pass_relu(size_t layer_idx);
    void forward_pass_sigmoid(size_t layer_idx);
    void forward_pass_tanh(size_t layer_idx);

    // ==================== FAST SIGMOID LOOKUP TABLE ====================
    static constexpr int SIGMOID_LUT_SIZE = 4096;
    static constexpr float SIGMOID_LUT_RANGE = 8.0f;  // Cover [-8, 8]
    static float sigmoid_lut[SIGMOID_LUT_SIZE];
    static bool sigmoid_lut_initialized;
    static void init_sigmoid_lut();

    // Activation functions (inline for performance)
    inline float relu(float x) const { return (x > 0.0f) ? x : 0.0f; }

    // Fast sigmoid using lookup table
    inline float sigmoid(float x) const {
        // Clamp to range
        if (x <= -SIGMOID_LUT_RANGE) return 0.0f;
        if (x >= SIGMOID_LUT_RANGE) return 1.0f;

        // Map x from [-8, 8] to [0, 4095]
        float normalized = (x + SIGMOID_LUT_RANGE) / (2.0f * SIGMOID_LUT_RANGE);
        int index = static_cast<int>(normalized * (SIGMOID_LUT_SIZE - 1));
        return sigmoid_lut[index];
    }

    inline float tanh_activation(float x) const { return std::tanh(x); }
    inline float linear(float x) const { return x; }

protected:
    static void _bind_methods();

public:
    NeuralNet();
    ~NeuralNet();

    void _ready() override;

    // ==================== NEURAL NETWORK INFERENCE ====================

    // Run inference on provided input features
    // Returns the network output value
    float predict(const Array &input_array);

    // ==================== NEURAL NETWORK UTILITIES ====================

    // Initialize neural network with custom architecture
    // layer_sizes: Array of layer sizes [input_size, hidden1_size, hidden2_size, ..., output_size]
    // default_activation: Default activation function ("linear", "relu", "sigmoid", "tanh"). Default is "sigmoid"
    // Example: [100, 64, 32, 1] creates a network with 100 inputs, 2 hidden layers (64 and 32 neurons), and 1 output
    void initialize_neural_network(const Array &layer_sizes_array, const String &default_activation = "sigmoid");

    // Save neural network to file (architecture + weights + biases)
    // Saves to res://models/ directory by default
    // Returns true on success
    bool save_network(const String &filename);

    // Load neural network from file (architecture + weights + biases)
    // Loads from res://models/ directory by default
    // Completely reinitializes the network with loaded data
    // Returns true on success
    bool load_network(const String &filename);

    // Set weights and biases for a specific layer
    // layer_index: Which layer to set (0 = first hidden layer, etc.)
    // weights_array: 2D array of weights [neuron_index][input_index]
    // biases_array: 1D array of biases [neuron_index]
    void set_layer_weights(int layer_index, const Array &weights_array, const Array &biases_array);

    // Set activation function for a specific hidden layer or all hidden layers
    // layer_index: Which hidden layer to set (-1 for all hidden layers, 0 for first hidden layer, etc.)
    // activation_type: "linear", "relu", "sigmoid", or "tanh"
    // Note: Output layer always uses sigmoid and cannot be changed
    void set_activation_function(int layer_index, const String &activation_type);

    // Get activation function for a specific hidden layer
    // Returns the activation function name, or empty string for invalid index
    String get_activation_function(int layer_index) const;

    // Get network architecture info
    bool is_network_initialized() const { return network_initialized; }
    Array get_layer_sizes() const;
    int get_num_layers() const { return layer_sizes.empty() ? 0 : static_cast<int>(layer_sizes.size() - 1); }

    // Get the expected input size for this network
    int get_input_size() const { return layer_sizes.empty() ? 0 : layer_sizes[0]; }

    // ==================== TRAINING METHODS ====================

    // Train on a single example (forward + backward pass + weight update)
    // input_features: The input vector (board state features)
    // target_output: The desired output (0.0 to 1.0)
    // learning_rate: Step size for gradient descent
    // Returns the loss (mean squared error)
    float train_single_example(const Array &input_features, float target_output, float learning_rate);

    // Backpropagation: Compute gradients for a single example
    // target_output: The desired output value
    void backpropagate(float target_output);

    // Update weights and biases using computed gradients
    // learning_rate: Step size for gradient descent
    void update_weights(float learning_rate);

    // Clear all gradients (reset to zero)
    void clear_gradients();

    // Derivative of activation functions
    inline float relu_derivative(float z) const { return (z > 0.0f) ? 1.0f : 0.0f; }
    inline float sigmoid_derivative(float activation) const { return activation * (1.0f - activation); }
    inline float tanh_derivative(float activation) const { return 1.0f - activation * activation; }
    inline float linear_derivative(float z) const { return 1.0f; }
};

#endif // NEURAL_NETWORK_H
