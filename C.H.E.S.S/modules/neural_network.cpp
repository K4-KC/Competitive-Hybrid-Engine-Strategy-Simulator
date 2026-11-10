#include "neural_network.h"
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <cstring>
#include <cmath>

using namespace godot;

// ==================== STATIC MEMBER DEFINITIONS ====================

float NeuralNet::sigmoid_lut[NeuralNet::SIGMOID_LUT_SIZE];
bool NeuralNet::sigmoid_lut_initialized = false;

void NeuralNet::init_sigmoid_lut() {
    if (sigmoid_lut_initialized) return;

    for (int i = 0; i < SIGMOID_LUT_SIZE; i++) {
        // Map index [0, 4095] to x in [-8, 8]
        float x = (static_cast<float>(i) / (SIGMOID_LUT_SIZE - 1)) * 2.0f * SIGMOID_LUT_RANGE - SIGMOID_LUT_RANGE;
        sigmoid_lut[i] = 1.0f / (1.0f + std::exp(-x));
    }

    sigmoid_lut_initialized = true;
}

// ==================== NEURAL NETWORK FORWARD PASS ====================

void NeuralNet::forward_pass_linear(size_t layer_idx) {
    int prev_layer_size = layer_sizes[layer_idx - 1];
    int current_layer_size = layer_sizes[layer_idx];
    size_t weight_idx = layer_idx - 1;

    for (int neuron = 0; neuron < current_layer_size; neuron++) {
        float sum = biases[weight_idx][neuron];
        for (int prev_neuron = 0; prev_neuron < prev_layer_size; prev_neuron++) {
            sum += activations[layer_idx - 1][prev_neuron] * weights[weight_idx][neuron][prev_neuron];
        }
        z_values[layer_idx][neuron] = sum;  // Store pre-activation value
        activations[layer_idx][neuron] = sum;  // Linear activation (no transformation)
    }
}

void NeuralNet::forward_pass_relu(size_t layer_idx) {
    const int prev_layer_size = layer_sizes[layer_idx - 1];
    const int current_layer_size = layer_sizes[layer_idx];
    const size_t weight_idx = layer_idx - 1;

    const float* prev_activations = activations[layer_idx - 1].data();
    float* curr_z_values = z_values[layer_idx].data();
    float* curr_activations = activations[layer_idx].data();

    for (int neuron = 0; neuron < current_layer_size; neuron++) {
        float sum = biases[weight_idx][neuron];
        const float* neuron_weights = weights[weight_idx][neuron].data();

        // Unroll by 4 for better performance
        int prev_neuron = 0;
        for (; prev_neuron + 3 < prev_layer_size; prev_neuron += 4) {
            sum += prev_activations[prev_neuron] * neuron_weights[prev_neuron];
            sum += prev_activations[prev_neuron + 1] * neuron_weights[prev_neuron + 1];
            sum += prev_activations[prev_neuron + 2] * neuron_weights[prev_neuron + 2];
            sum += prev_activations[prev_neuron + 3] * neuron_weights[prev_neuron + 3];
        }
        // Handle remainder
        for (; prev_neuron < prev_layer_size; prev_neuron++) {
            sum += prev_activations[prev_neuron] * neuron_weights[prev_neuron];
        }

        curr_z_values[neuron] = sum;
        curr_activations[neuron] = relu(sum);
    }
}

void NeuralNet::forward_pass_sigmoid(size_t layer_idx) {
    const int prev_layer_size = layer_sizes[layer_idx - 1];
    const int current_layer_size = layer_sizes[layer_idx];
    const size_t weight_idx = layer_idx - 1;

    const float* prev_activations = activations[layer_idx - 1].data();
    float* curr_z_values = z_values[layer_idx].data();
    float* curr_activations = activations[layer_idx].data();

    for (int neuron = 0; neuron < current_layer_size; neuron++) {
        float sum = biases[weight_idx][neuron];
        const float* neuron_weights = weights[weight_idx][neuron].data();

        // Unroll by 4 for better performance
        int prev_neuron = 0;
        for (; prev_neuron + 3 < prev_layer_size; prev_neuron += 4) {
            sum += prev_activations[prev_neuron] * neuron_weights[prev_neuron];
            sum += prev_activations[prev_neuron + 1] * neuron_weights[prev_neuron + 1];
            sum += prev_activations[prev_neuron + 2] * neuron_weights[prev_neuron + 2];
            sum += prev_activations[prev_neuron + 3] * neuron_weights[prev_neuron + 3];
        }
        // Handle remainder
        for (; prev_neuron < prev_layer_size; prev_neuron++) {
            sum += prev_activations[prev_neuron] * neuron_weights[prev_neuron];
        }

        curr_z_values[neuron] = sum;
        curr_activations[neuron] = sigmoid(sum);
    }
}

void NeuralNet::forward_pass_tanh(size_t layer_idx) {
    int prev_layer_size = layer_sizes[layer_idx - 1];
    int current_layer_size = layer_sizes[layer_idx];
    size_t weight_idx = layer_idx - 1;

    for (int neuron = 0; neuron < current_layer_size; neuron++) {
        float sum = biases[weight_idx][neuron];
        for (int prev_neuron = 0; prev_neuron < prev_layer_size; prev_neuron++) {
            sum += activations[layer_idx - 1][prev_neuron] * weights[weight_idx][neuron][prev_neuron];
        }
        z_values[layer_idx][neuron] = sum;  // Store pre-activation value
        activations[layer_idx][neuron] = tanh_activation(sum);
    }
}

float NeuralNet::forward_pass(const std::vector<float> &input_features) {
    // If network is not initialized, return 0.5 (neutral)
    if (!network_initialized || layer_sizes.empty()) {
        return 0.5f;
    }

    // Validate input size
    if (input_features.size() != static_cast<size_t>(layer_sizes[0])) {
        UtilityFunctions::print("Error: Input size mismatch. Expected ", layer_sizes[0],
                                ", got ", static_cast<int64_t>(input_features.size()));
        return 0.5f;
    }

    // Check if weights are set
    if (weights.empty() || biases.empty()) {
        UtilityFunctions::print("Error: Weights not set");
        return 0.5f;
    }

    // Ensure activations storage is properly sized
    if (activations.size() != layer_sizes.size()) {
        activations.resize(layer_sizes.size());
        for (size_t i = 0; i < layer_sizes.size(); i++) {
            activations[i].resize(layer_sizes[i]);
        }
    }

    // Set input layer activations
    for (size_t i = 0; i < input_features.size(); i++) {
        activations[0][i] = input_features[i];
    }

    // Forward pass through hidden layers
    size_t num_layers = layer_sizes.size();
    for (size_t layer = 1; layer < num_layers - 1; layer++) {
        // Get activation type for this layer
        int activation_type = (layer - 1 < activation_functions.size()) ?
                              activation_functions[layer - 1] : 2;  // Default to sigmoid

        // Call specialized function based on activation type
        switch (activation_type) {
            case 0: forward_pass_linear(layer); break;
            case 1: forward_pass_relu(layer); break;
            case 2: forward_pass_sigmoid(layer); break;
            case 3: forward_pass_tanh(layer); break;
            default: forward_pass_sigmoid(layer); break;
        }
    }

    // Output layer (always uses sigmoid to keep output between 0 and 1)
    if (num_layers > 1) {
        const size_t output_layer = num_layers - 1;
        const int prev_layer_size = layer_sizes[output_layer - 1];
        const size_t weight_idx = output_layer - 1;

        // Compute output (single neuron with sigmoid)
        float sum = biases[weight_idx][0];
        const float* prev_activations = activations[output_layer - 1].data();
        const float* output_weights = weights[weight_idx][0].data();

        // Unroll by 4 for better performance
        int prev_neuron = 0;
        for (; prev_neuron + 3 < prev_layer_size; prev_neuron += 4) {
            sum += prev_activations[prev_neuron] * output_weights[prev_neuron];
            sum += prev_activations[prev_neuron + 1] * output_weights[prev_neuron + 1];
            sum += prev_activations[prev_neuron + 2] * output_weights[prev_neuron + 2];
            sum += prev_activations[prev_neuron + 3] * output_weights[prev_neuron + 3];
        }
        // Handle remainder
        for (; prev_neuron < prev_layer_size; prev_neuron++) {
            sum += prev_activations[prev_neuron] * output_weights[prev_neuron];
        }

        // Apply sigmoid to keep output between 0 and 1
        float output = sigmoid(sum);
        activations[output_layer][0] = output;

        return output;
    }

    return 0.5f;  // Should never reach here
}

// ==================== NEURAL NETWORK INFERENCE ====================

float NeuralNet::predict(const Array &input_array) {
    // Convert Array to std::vector<float>
    std::vector<float> input_vec;
    input_vec.reserve(input_array.size());

    for (int i = 0; i < input_array.size(); i++) {
        input_vec.push_back(input_array[i]);
    }

    return forward_pass(input_vec);
}

// ==================== NEURAL NETWORK UTILITIES ====================

int NeuralNet::activation_string_to_int(const String &activation_str) const {
    String lower = activation_str.to_lower();
    if (lower == "linear") return 0;
    if (lower == "relu") return 1;
    if (lower == "sigmoid") return 2;
    if (lower == "tanh") return 3;
    return -1;  // Invalid
}

String NeuralNet::activation_int_to_string(int activation_type) const {
    switch (activation_type) {
        case 0: return "linear";
        case 1: return "relu";
        case 2: return "sigmoid";
        case 3: return "tanh";
        default: return "";
    }
}

void NeuralNet::initialize_neural_network(const Array &layer_sizes_array, const String &default_activation /* = "sigmoid" */) {
    // Clear existing network
    layer_sizes.clear();
    weights.clear();
    biases.clear();
    activations.clear();
    activation_functions.clear();
    network_initialized = false;

    // Validate input
    if (layer_sizes_array.size() < 2) {
        UtilityFunctions::print("Error: Neural network must have at least 2 layers (input and output)");
        return;
    }

    // Convert activation string to int
    int activation = activation_string_to_int(default_activation);
    if (activation < 0) {
        UtilityFunctions::print("Warning: Invalid activation type '", default_activation,
                                "'. Valid options: linear, relu, sigmoid, tanh. Using sigmoid as default.");
        activation = 2;
    }

    // Parse layer sizes
    for (int i = 0; i < layer_sizes_array.size(); i++) {
        int size = layer_sizes_array[i];
        if (size <= 0) {
            UtilityFunctions::print("Error: Layer size must be positive, got ", size, " at index ", i);
            return;
        }
        layer_sizes.push_back(size);
    }

    // Initialize weights and biases with random values (Xavier initialization)
    int num_weight_layers = layer_sizes.size() - 1;
    weights.resize(num_weight_layers);
    biases.resize(num_weight_layers);

    // Initialize activation functions for each layer
    // Note: Output layer always uses sigmoid (enforced in forward_pass)
    // So we only store activations for hidden layers
    activation_functions.resize(num_weight_layers - 1);  // Exclude output layer
    for (int i = 0; i < num_weight_layers - 1; i++) {
        activation_functions[i] = activation;
    }

    for (int layer = 0; layer < num_weight_layers; layer++) {
        int input_size = layer_sizes[layer];
        int output_size = layer_sizes[layer + 1];

        // Initialize weights for this layer
        weights[layer].resize(output_size);
        biases[layer].resize(output_size);

        // Xavier initialization factor
        float xavier_factor = std::sqrt(2.0f / (input_size + output_size));

        for (int neuron = 0; neuron < output_size; neuron++) {
            weights[layer][neuron].resize(input_size);

            // Initialize weights with small random values
            for (int input = 0; input < input_size; input++) {
                // Simple random initialization (in practice, you'd load these from a file)
                weights[layer][neuron][input] = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX) - 0.5f) * 2.0f * xavier_factor;
            }

            // Initialize bias to zero
            biases[layer][neuron] = 0.0f;
        }
    }

    // Initialize activation storage
    activations.resize(layer_sizes.size());
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        activations[i].resize(layer_sizes[i]);
    }

    // Initialize training-related storage
    weight_gradients.resize(num_weight_layers);
    bias_gradients.resize(num_weight_layers);
    z_values.resize(layer_sizes.size());
    deltas.resize(layer_sizes.size());

    for (int layer = 0; layer < num_weight_layers; layer++) {
        int input_size = layer_sizes[layer];
        int output_size = layer_sizes[layer + 1];

        weight_gradients[layer].resize(output_size);
        bias_gradients[layer].resize(output_size, 0.0f);

        for (int neuron = 0; neuron < output_size; neuron++) {
            weight_gradients[layer][neuron].resize(input_size, 0.0f);
        }
    }

    for (size_t i = 0; i < layer_sizes.size(); i++) {
        z_values[i].resize(layer_sizes[i], 0.0f);
        deltas[i].resize(layer_sizes[i], 0.0f);
    }

    network_initialized = true;

    // Print network architecture with activation functions
    UtilityFunctions::print("Neural network initialized with architecture:");
    String arch = "  [";
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        arch += String::num_int64(layer_sizes[i]);
        if (i < layer_sizes.size() - 1) arch += ", ";
    }
    arch += "]";
    UtilityFunctions::print(arch);

    // Print activation functions (hidden layers + output)
    String activation_names[] = {"linear", "relu", "sigmoid", "tanh"};
    String activations_str = "  Activations: [";
    for (size_t i = 0; i < activation_functions.size(); i++) {
        int act_type = activation_functions[i];
        activations_str += (act_type >= 0 && act_type <= 3) ? activation_names[act_type] : "unknown";
        activations_str += ", ";
    }
    activations_str += "sigmoid]";  // Output layer always uses sigmoid
    UtilityFunctions::print(activations_str);
}

void NeuralNet::set_layer_weights(int layer_index, const Array &weights_array, const Array &biases_array) {
    if (!network_initialized) {
        UtilityFunctions::print("Error: Network not initialized. Call initialize_neural_network first.");
        return;
    }

    if (layer_index < 0 || layer_index >= static_cast<int>(weights.size())) {
        UtilityFunctions::print("Error: Invalid layer index ", layer_index,
                                ". Valid range: 0 to ", static_cast<int64_t>(weights.size() - 1));
        return;
    }

    int output_size = layer_sizes[layer_index + 1];
    int input_size = layer_sizes[layer_index];

    // Validate weights array dimensions
    if (weights_array.size() != output_size) {
        UtilityFunctions::print("Error: Expected ", output_size, " neurons, got ", weights_array.size());
        return;
    }

    // Validate biases array
    if (biases_array.size() != output_size) {
        UtilityFunctions::print("Error: Expected ", output_size, " biases, got ", biases_array.size());
        return;
    }

    // Set weights
    for (int neuron = 0; neuron < output_size; neuron++) {
        Array neuron_weights = weights_array[neuron];

        if (neuron_weights.size() != input_size) {
            UtilityFunctions::print("Error: Neuron ", neuron, " expected ", input_size,
                                    " weights, got ", neuron_weights.size());
            return;
        }

        for (int input = 0; input < input_size; input++) {
            weights[layer_index][neuron][input] = neuron_weights[input];
        }

        biases[layer_index][neuron] = biases_array[neuron];
    }

    UtilityFunctions::print("Layer ", layer_index, " weights and biases set successfully");
}

Array NeuralNet::get_layer_sizes() const {
    Array result;
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        result.append(layer_sizes[i]);
    }
    return result;
}

void NeuralNet::set_activation_function(int layer_index, const String &activation_type) {
    if (!network_initialized) {
        UtilityFunctions::print("Error: Network not initialized. Call initialize_neural_network first.");
        return;
    }

    // Convert activation string to int
    int activation = activation_string_to_int(activation_type);
    if (activation < 0) {
        UtilityFunctions::print("Error: Invalid activation type '", activation_type,
                                "'. Valid options: linear, relu, sigmoid, tanh");
        return;
    }

    // Set for all hidden layers if layer_index is -1
    if (layer_index == -1) {
        for (size_t i = 0; i < activation_functions.size(); i++) {
            activation_functions[i] = activation;
        }
        UtilityFunctions::print("All hidden layers set to activation '", activation_type, "'");
        UtilityFunctions::print("Note: Output layer always uses sigmoid");
        return;
    }

    // Validate layer index (can only set hidden layers, not output)
    if (layer_index < 0 || layer_index >= static_cast<int>(activation_functions.size())) {
        UtilityFunctions::print("Error: Invalid layer index ", layer_index,
                                ". Valid range: 0 to ", static_cast<int64_t>(activation_functions.size() - 1),
                                " (or -1 for all hidden layers)");
        UtilityFunctions::print("Note: Output layer activation cannot be changed (always sigmoid)");
        return;
    }

    activation_functions[layer_index] = activation;
    UtilityFunctions::print("Hidden layer ", layer_index, " activation set to '", activation_type, "'");
}

String NeuralNet::get_activation_function(int layer_index) const {
    if (!network_initialized) {
        UtilityFunctions::print("Error: Network not initialized.");
        return "";
    }

    if (layer_index < 0 || layer_index >= static_cast<int>(activation_functions.size())) {
        UtilityFunctions::print("Error: Invalid layer index ", layer_index);
        return "";
    }

    return activation_int_to_string(activation_functions[layer_index]);
}

bool NeuralNet::save_network(const String &filename) {
    if (!network_initialized) {
        UtilityFunctions::print("Error: Cannot save uninitialized network");
        return false;
    }

    // Ensure models directory exists
    Ref<DirAccess> dir = DirAccess::open("res://");
    if (dir.is_null()) {
        UtilityFunctions::print("Error: Cannot access res:// directory");
        return false;
    }

    if (!dir->dir_exists("models")) {
        Error err = dir->make_dir("models");
        if (err != OK) {
            UtilityFunctions::print("Error: Cannot create models directory");
            return false;
        }
    }

    // Construct full path
    String full_path = "res://models/" + filename;
    if (!full_path.ends_with(".nn")) {
        full_path += ".nn";
    }

    // Open file for writing
    Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::WRITE);
    if (file.is_null()) {
        UtilityFunctions::print("Error: Cannot open file for writing: ", full_path);
        return false;
    }

    // ==================== FILE FORMAT ====================
    // Magic number (4 bytes): "NNWB" (Neural Network Weights Binary)
    // Version (4 bytes): 1
    // Num layers (4 bytes)
    // Layer sizes (num_layers * 4 bytes)
    // Num hidden layers (4 bytes)
    // Activation functions (num_hidden_layers * 4 bytes)
    // For each weight layer:
    //   - Num weights (4 bytes)
    //   - Weights (num_weights * 4 bytes as floats)
    //   - Num biases (4 bytes)
    //   - Biases (num_biases * 4 bytes as floats)
    // ==================== END FORMAT ====================

    // Write magic number
    file->store_8('N');
    file->store_8('N');
    file->store_8('W');
    file->store_8('B');

    // Write version
    file->store_32(1);

    // Write layer sizes
    file->store_32(layer_sizes.size());
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        file->store_32(layer_sizes[i]);
    }

    // Write activation functions
    file->store_32(activation_functions.size());
    for (size_t i = 0; i < activation_functions.size(); i++) {
        file->store_32(activation_functions[i]);
    }

    // Write weights and biases
    for (size_t layer = 0; layer < weights.size(); layer++) {
        int output_size = layer_sizes[layer + 1];
        int input_size = layer_sizes[layer];

        // Write weights for this layer
        file->store_32(output_size * input_size);
        for (int neuron = 0; neuron < output_size; neuron++) {
            for (int input = 0; input < input_size; input++) {
                file->store_float(weights[layer][neuron][input]);
            }
        }

        // Write biases for this layer
        file->store_32(output_size);
        for (int neuron = 0; neuron < output_size; neuron++) {
            file->store_float(biases[layer][neuron]);
        }
    }

    file->close();

    UtilityFunctions::print("Neural network saved successfully to ", full_path);
    return true;
}

bool NeuralNet::load_network(const String &filename) {
    // Construct full path
    String full_path = "res://models/" + filename;
    if (!full_path.ends_with(".nn")) {
        full_path += ".nn";
    }

    // Open file for reading
    Ref<FileAccess> file = FileAccess::open(full_path, FileAccess::READ);
    if (file.is_null()) {
        UtilityFunctions::print("Error: Cannot open file for reading: ", full_path);
        return false;
    }

    // Read and validate magic number
    char magic[4];
    magic[0] = file->get_8();
    magic[1] = file->get_8();
    magic[2] = file->get_8();
    magic[3] = file->get_8();

    if (magic[0] != 'N' || magic[1] != 'N' || magic[2] != 'W' || magic[3] != 'B') {
        UtilityFunctions::print("Error: Invalid file format (bad magic number)");
        file->close();
        return false;
    }

    // Read version
    uint32_t version = file->get_32();
    if (version != 1) {
        UtilityFunctions::print("Error: Unsupported file version: ", version);
        file->close();
        return false;
    }

    // Clear existing network
    layer_sizes.clear();
    weights.clear();
    biases.clear();
    activations.clear();
    activation_functions.clear();
    network_initialized = false;

    // Read layer sizes
    uint32_t num_layers = file->get_32();
    layer_sizes.resize(num_layers);
    for (uint32_t i = 0; i < num_layers; i++) {
        layer_sizes[i] = file->get_32();
    }

    // Read activation functions
    uint32_t num_activations = file->get_32();
    activation_functions.resize(num_activations);
    for (uint32_t i = 0; i < num_activations; i++) {
        activation_functions[i] = file->get_32();
    }

    // Initialize weight and bias storage
    int num_weight_layers = layer_sizes.size() - 1;
    weights.resize(num_weight_layers);
    biases.resize(num_weight_layers);

    // Read weights and biases
    for (int layer = 0; layer < num_weight_layers; layer++) {
        int output_size = layer_sizes[layer + 1];
        int input_size = layer_sizes[layer];

        // Read weights
        uint32_t num_weights = file->get_32();
        if (num_weights != static_cast<uint32_t>(output_size * input_size)) {
            UtilityFunctions::print("Error: Weight count mismatch at layer ", layer);
            file->close();
            return false;
        }

        weights[layer].resize(output_size);
        for (int neuron = 0; neuron < output_size; neuron++) {
            weights[layer][neuron].resize(input_size);
            for (int input = 0; input < input_size; input++) {
                weights[layer][neuron][input] = file->get_float();
            }
        }

        // Read biases
        uint32_t num_biases = file->get_32();
        if (num_biases != static_cast<uint32_t>(output_size)) {
            UtilityFunctions::print("Error: Bias count mismatch at layer ", layer);
            file->close();
            return false;
        }

        biases[layer].resize(output_size);
        for (int neuron = 0; neuron < output_size; neuron++) {
            biases[layer][neuron] = file->get_float();
        }
    }

    // Initialize activation storage
    activations.resize(layer_sizes.size());
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        activations[i].resize(layer_sizes[i]);
    }

    network_initialized = true;

    file->close();

    // Print loaded network info
    UtilityFunctions::print("Neural network loaded successfully from ", full_path);
    String arch = "  Architecture: [";
    for (size_t i = 0; i < layer_sizes.size(); i++) {
        arch += String::num_int64(layer_sizes[i]);
        if (i < layer_sizes.size() - 1) arch += ", ";
    }
    arch += "]";
    UtilityFunctions::print(arch);

    // Print activation functions
    String activation_names[] = {"linear", "relu", "sigmoid", "tanh"};
    String activations_str = "  Activations: [";
    for (size_t i = 0; i < activation_functions.size(); i++) {
        int act_type = activation_functions[i];
        activations_str += (act_type >= 0 && act_type <= 3) ? activation_names[act_type] : "unknown";
        activations_str += ", ";
    }
    activations_str += "sigmoid]";  // Output layer always uses sigmoid
    UtilityFunctions::print(activations_str);

    return true;
}

// ==================== CONSTRUCTOR/DESTRUCTOR ====================

NeuralNet::NeuralNet() {
    network_initialized = false;
    init_sigmoid_lut();
}

NeuralNet::~NeuralNet() {
}

void NeuralNet::_ready() {
}

// ==================== TRAINING METHODS ====================

void NeuralNet::clear_gradients() {
    for (size_t layer = 0; layer < weight_gradients.size(); layer++) {
        for (size_t neuron = 0; neuron < weight_gradients[layer].size(); neuron++) {
            for (size_t input = 0; input < weight_gradients[layer][neuron].size(); input++) {
                weight_gradients[layer][neuron][input] = 0.0f;
            }
            bias_gradients[layer][neuron] = 0.0f;
        }
    }
}

void NeuralNet::backpropagate(float target_output) {
    if (!network_initialized) {
        return;
    }

    int num_layers = layer_sizes.size();

    // 1. Compute output layer delta (error signal)
    int output_layer = num_layers - 1;
    float output = activations[output_layer][0];

    // dL/doutput = output - target (for MSE loss)
    float output_error = output - target_output;

    // delta = dL/doutput * sigmoid'(output)
    deltas[output_layer][0] = output_error * sigmoid_derivative(output);

    // 2. Backpropagate through hidden layers
    for (int layer = num_layers - 2; layer >= 1; layer--) {
        int current_size = layer_sizes[layer];
        int next_size = layer_sizes[layer + 1];
        int activation_type = (layer - 1 < static_cast<int>(activation_functions.size())) ?
                              activation_functions[layer - 1] : 2;

        for (int neuron = 0; neuron < current_size; neuron++) {
            float sum = 0.0f;

            // Sum weighted deltas from next layer
            for (int next_neuron = 0; next_neuron < next_size; next_neuron++) {
                sum += deltas[layer + 1][next_neuron] * weights[layer][next_neuron][neuron];
            }

            // Apply derivative of activation function
            float derivative;
            switch (activation_type) {
                case 0: // linear
                    derivative = linear_derivative(z_values[layer][neuron]);
                    break;
                case 1: // relu
                    derivative = relu_derivative(z_values[layer][neuron]);
                    break;
                case 2: // sigmoid
                    derivative = sigmoid_derivative(activations[layer][neuron]);
                    break;
                case 3: // tanh
                    derivative = tanh_derivative(activations[layer][neuron]);
                    break;
                default:
                    derivative = sigmoid_derivative(activations[layer][neuron]);
            }

            deltas[layer][neuron] = sum * derivative;
        }
    }

    // 3. Compute gradients for all layers
    for (int layer = 0; layer < num_layers - 1; layer++) {
        int prev_size = layer_sizes[layer];
        int curr_size = layer_sizes[layer + 1];

        for (int neuron = 0; neuron < curr_size; neuron++) {
            // Bias gradient
            bias_gradients[layer][neuron] += deltas[layer + 1][neuron];

            // Weight gradients
            for (int prev_neuron = 0; prev_neuron < prev_size; prev_neuron++) {
                weight_gradients[layer][neuron][prev_neuron] +=
                    deltas[layer + 1][neuron] * activations[layer][prev_neuron];
            }
        }
    }
}

void NeuralNet::update_weights(float learning_rate) {
    if (!network_initialized) {
        return;
    }

    // Update all weights and biases using gradient descent
    for (size_t layer = 0; layer < weights.size(); layer++) {
        for (size_t neuron = 0; neuron < weights[layer].size(); neuron++) {
            // Update bias
            biases[layer][neuron] -= learning_rate * bias_gradients[layer][neuron];

            // Update weights
            for (size_t input = 0; input < weights[layer][neuron].size(); input++) {
                weights[layer][neuron][input] -= learning_rate * weight_gradients[layer][neuron][input];
            }
        }
    }
}

float NeuralNet::train_single_example(const Array &input_array, float target_output, float learning_rate) {
    if (!network_initialized) {
        UtilityFunctions::print("Error: Network not initialized");
        return 0.0f;
    }

    // Convert Array to std::vector<float>
    std::vector<float> input_vec;
    input_vec.reserve(input_array.size());
    for (int i = 0; i < input_array.size(); i++) {
        input_vec.push_back(input_array[i]);
    }

    // 1. Forward pass (also stores activations and z-values)
    float output = forward_pass(input_vec);

    // 2. Compute loss (Mean Squared Error)
    float error = output - target_output;
    float loss = error * error;

    // 3. Clear previous gradients
    clear_gradients();

    // 4. Backpropagation (compute gradients)
    backpropagate(target_output);

    // 5. Update weights
    update_weights(learning_rate);

    return loss;
}

// ==================== GODOT BINDINGS ====================

void NeuralNet::_bind_methods() {
    // Neural network inference
    ClassDB::bind_method(D_METHOD("predict", "input_array"), &NeuralNet::predict);

    // Neural network utilities
    ClassDB::bind_method(D_METHOD("initialize_neural_network", "layer_sizes", "activation"), &NeuralNet::initialize_neural_network, DEFVAL("sigmoid"));
    ClassDB::bind_method(D_METHOD("save_network", "filename"), &NeuralNet::save_network);
    ClassDB::bind_method(D_METHOD("load_network", "filename"), &NeuralNet::load_network);
    ClassDB::bind_method(D_METHOD("set_layer_weights", "layer_index", "weights", "biases"), &NeuralNet::set_layer_weights);
    ClassDB::bind_method(D_METHOD("set_activation_function", "layer_index", "activation_type"), &NeuralNet::set_activation_function);
    ClassDB::bind_method(D_METHOD("get_activation_function", "layer_index"), &NeuralNet::get_activation_function);
    ClassDB::bind_method(D_METHOD("is_network_initialized"), &NeuralNet::is_network_initialized);
    ClassDB::bind_method(D_METHOD("get_layer_sizes"), &NeuralNet::get_layer_sizes);
    ClassDB::bind_method(D_METHOD("get_num_layers"), &NeuralNet::get_num_layers);
    ClassDB::bind_method(D_METHOD("get_input_size"), &NeuralNet::get_input_size);

    // Training methods
    ClassDB::bind_method(D_METHOD("train_single_example", "input_features", "target_output", "learning_rate"), &NeuralNet::train_single_example);
}

