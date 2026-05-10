extends Node2D

# Computer vs Computer Mode
# Two AI agents play against each other.
# Supports training mode (save models) and inference mode (load/use existing models).

const TILE_SIZE = 16
const BOARD_OFFSET = Vector2(8, 8)

# AI Configuration
const AI_DEPTH = 5  # Maximum search depth for Iterative Deepening

# Training Configuration
# TRAINING_MODE options:
#   0 = "none" - No training (inference only)
#   1 = "heuristic" - Train using material evaluation as supervision
#   2 = "distillation" - Train using tree search evaluations (requires pre-trained model)
var TRAINING_MODE = 1  # 0=none, 1=heuristic, 2=distillation
const SAVE_MODELS_AFTER_GAME = true  # Auto-save models after game ends
const MODEL_WHITE_PATH = "res://assets/models/white_agent.nn"
const MODEL_BLACK_PATH = "res://assets/models/black_agent.nn"
const LOAD_MODELS_ON_START = true  # Set to true to load existing models
const LEARNING_RATE = 0.001  # Learning rate for gradient descent
const TRAIN_EVERY_N_MOVES = 1  # Train after every N moves (1 = train after each move)
const DISTILLATION_SEARCH_DEPTH = 3  # Depth for tree search in distillation mode

# Piece type constants (must match board.h)
const PIECE_NONE = 0
const PIECE_PAWN = 1
const PIECE_KNIGHT = 2
const PIECE_BISHOP = 3
const PIECE_ROOK = 4
const PIECE_QUEEN = 5
const PIECE_KING = 6

# Color constants (must match board.h)
const COLOR_WHITE = 8   # 0b01000
const COLOR_BLACK = 16  # 0b10000

# Masks
const PIECE_TYPE_MASK = 7   # 0b00111
const COLOR_MASK = 24       # 0b11000

var board: Board
var white_agent: Agent  # Agent playing White
var black_agent: Agent  # Agent playing Black
var sprites = {} # Map<Vector2i, Sprite2D>

# AI state
var ai_thinking: bool = false
var ai_thread: Thread = null
var current_thinking_color: int = 0  # Which agent is thinking

# Temporary visual helpers
var last_move_sprites = []

# Sprite atlas (board_sheet.png + board_sheet.json)
var slices_data = {}
var texture_cache = {}
var spritesheet_texture: Texture2D

# UI nodes
var thinking_label: Label = null
var status_label: Label = null

# Game management
var move_count: int = 0
var game_finished: bool = false

# Training data
var training_positions = []  # Stores board positions (feature vectors) from the game
var training_targets = []    # Stores target values for each position
var total_training_loss = 0.0
var num_training_examples = 0

func _ready():
	# 0. Load the spritesheet atlas before anything that needs textures
	load_aseprite_assets("res://assets/sprites/board_sheet.json", "res://assets/sprites/board_sheet.png")

	var hl_node = Node2D.new()
	hl_node.name = "Highlights"
	add_child(hl_node)

	var pieces_node = Node2D.new()
	pieces_node.name = "Pieces"
	add_child(pieces_node)

	# 1. Initialize the Board (State)
	board = Board.new()
	add_child(board)

	# 2. Initialize both AI Agents
	white_agent = Agent.new()
	white_agent.name = "WhiteAgent"
	add_child(white_agent)
	white_agent.set_board(board)

	black_agent = Agent.new()
	black_agent.name = "BlackAgent"
	add_child(black_agent)
	black_agent.set_board(board)

	# 3. Configure agents based on training mode
	setup_agents()

	setup_ui()

	# Default to standard start if no FEN was passed
	var start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

	# Check if the Main Menu passed us a FEN string
	if get_tree().root.has_meta("start_fen"):
		start_fen = get_tree().root.get_meta("start_fen")

	board.setup_board(start_fen)
	refresh_visuals()

	var training_mode_names = ["None (Inference Only)", "Heuristic (Material Eval)", "Distillation (Tree Search)"]
	print("Computer vs Computer Mode Ready.")
	print("Training Mode: %s" % training_mode_names[TRAINING_MODE])
	if TRAINING_MODE == 2:
		print("  Distillation Search Depth: %d" % DISTILLATION_SEARCH_DEPTH)
	print("AI Max Depth: %d (Iterative Deepening)" % AI_DEPTH)
	print("White Agent: Neural Network %s" % ("Enabled" if white_agent.get_use_neural_network() else "Disabled"))
	print("Black Agent: Neural Network %s" % ("Enabled" if black_agent.get_use_neural_network() else "Disabled"))
	print(start_fen)

	# Start the game
	call_deferred("make_next_ai_move")

func setup_agents():
	"""Configure agents based on training mode and load models if needed."""

	if LOAD_MODELS_ON_START:
		# Attempt to load existing models
		print("\nAttempting to load existing models...")

		var white_loaded = white_agent.load_network(MODEL_WHITE_PATH)
		if white_loaded:
			print("White agent model loaded successfully from: %s" % MODEL_WHITE_PATH)
			white_agent.set_use_neural_network(true)
		else:
			print("White agent model not found or failed to load. Using material evaluation.")
			white_agent.set_use_neural_network(false)

		var black_loaded = black_agent.load_network(MODEL_BLACK_PATH)
		if black_loaded:
			print("Black agent model loaded successfully from: %s" % MODEL_BLACK_PATH)
			black_agent.set_use_neural_network(true)
		else:
			print("Black agent model not found or failed to load. Using material evaluation.")
			black_agent.set_use_neural_network(false)
	else:
		# Initialize fresh neural networks or use material evaluation
		if TRAINING_MODE == 1:
			# Heuristic training: Initialize neural networks with random weights
			print("\nInitializing neural networks for heuristic training...")

			# Network architecture: 781 inputs (board features) -> hidden layers -> 1 output
			# Using a simple architecture: 781 -> 256 -> 128 -> 1
			var layer_sizes = [781, 256, 128, 1]

			white_agent.initialize_neural_network(layer_sizes, "relu")
			white_agent.set_use_neural_network(true)
			print("White agent initialized with architecture: %s" % str(layer_sizes))

			black_agent.initialize_neural_network(layer_sizes, "relu")
			black_agent.set_use_neural_network(true)
			print("Black agent initialized with architecture: %s" % str(layer_sizes))

		elif TRAINING_MODE == 2:
			# Distillation training: MUST load existing models
			print("\nDistillation mode requires pre-trained models...")

			var white_loaded = white_agent.load_network(MODEL_WHITE_PATH)
			var black_loaded = black_agent.load_network(MODEL_BLACK_PATH)

			if not white_loaded or not black_loaded:
				print("ERROR: Distillation mode requires existing trained models!")
				print("Please run heuristic training first (TRAINING_MODE = 1)")
				print("Falling back to material evaluation only.")
				white_agent.set_use_neural_network(false)
				black_agent.set_use_neural_network(false)
				TRAINING_MODE = 0
			else:
				white_agent.set_use_neural_network(true)
				black_agent.set_use_neural_network(true)
				print("Models loaded successfully for distillation training")
		else:
			# No training mode: use material evaluation only
			print("\nUsing material evaluation (no neural network).")
			white_agent.set_use_neural_network(false)
			black_agent.set_use_neural_network(false)

# --- Aseprite atlas loading ---

func load_aseprite_assets(json_path: String, image_path: String):
	spritesheet_texture = load(image_path)
	if not spritesheet_texture:
		push_error("Failed to load spritesheet at: " + image_path)
		return

	if not FileAccess.file_exists(json_path):
		push_error("JSON file not found: " + json_path)
		return

	var file = FileAccess.open(json_path, FileAccess.READ)
	var json_text = file.get_as_text()
	var json = JSON.new()
	var error = json.parse(json_text)
	if error != OK:
		push_error("JSON Parse Error: " + json.get_error_message())
		return

	var data = json.data
	if data.has("meta") and data["meta"].has("slices"):
		for slice in data["meta"]["slices"]:
			var s_name = slice["name"]
			var bounds = slice["keys"][0]["bounds"]
			slices_data[s_name] = Rect2(bounds["x"], bounds["y"], bounds["w"], bounds["h"])
	else:
		push_error("No slices found in JSON meta data!")

func get_slice_texture(slice_name: String) -> AtlasTexture:
	if texture_cache.has(slice_name):
		return texture_cache[slice_name]

	var atlas = AtlasTexture.new()
	atlas.atlas = spritesheet_texture
	atlas.region = slices_data[slice_name]
	texture_cache[slice_name] = atlas
	return atlas

func piece_texture(color: int, type: String) -> AtlasTexture:
	return get_slice_texture(type + str(color))

func setup_ui():
	var canvas = CanvasLayer.new()
	add_child(canvas)

	# AI thinking indicator label
	thinking_label = Label.new()
	thinking_label.text = "White thinking..."
	thinking_label.visible = false
	thinking_label.position = Vector2(10, 140)
	thinking_label.add_theme_font_size_override("font_size", 8)
	canvas.add_child(thinking_label)

	# Status label
	status_label = Label.new()
	status_label.text = "Move 0"
	status_label.position = Vector2(10, 150)
	status_label.add_theme_font_size_override("font_size", 8)
	canvas.add_child(status_label)

# --- Conversion Helpers ---

func grid_to_square(grid_pos: Vector2i) -> int:
	var file = grid_pos.x
	var rank = 7 - grid_pos.y
	return rank * 8 + file

func square_to_grid(square: int) -> Vector2i:
	var file = square % 8
	@warning_ignore("integer_division")
	var rank = square / 8
	return Vector2i(file, 7 - rank)

func get_data_at(grid_pos: Vector2i) -> Dictionary:
	var square = grid_to_square(grid_pos)
	var piece_value = board.get_piece_on_square(square)
	return decode_piece(piece_value)

func decode_piece(piece_value: int) -> Dictionary:
	var piece_type = piece_value & PIECE_TYPE_MASK
	var color_bits = piece_value & COLOR_MASK

	if piece_type == PIECE_NONE:
		return {}

	var type_str = ""
	match piece_type:
		PIECE_PAWN: type_str = "p"
		PIECE_KNIGHT: type_str = "n"
		PIECE_BISHOP: type_str = "b"
		PIECE_ROOK: type_str = "r"
		PIECE_QUEEN: type_str = "q"
		PIECE_KING: type_str = "k"

	var color = 0 if color_bits == COLOR_WHITE else 1
	return {"type": type_str, "color": color}

func update_last_move_visuals(start: Vector2i, end: Vector2i):
	for s in last_move_sprites:
		s.queue_free()
	last_move_sprites.clear()

	var moved_tex = get_slice_texture("moved")
	var s1 = Sprite2D.new()
	s1.texture = moved_tex
	s1.position = grid_to_pixel(Vector2(start.x, start.y))
	$Highlights.add_child(s1)
	last_move_sprites.append(s1)

	var s2 = Sprite2D.new()
	s2.texture = moved_tex
	s2.position = grid_to_pixel(Vector2(end.x, end.y))
	$Highlights.add_child(s2)
	last_move_sprites.append(s2)

func refresh_visuals():
	var active_positions = []
	for x in range(8):
		for y in range(8):
			var pos = Vector2i(x, y)
			var data = get_data_at(pos)
			if data.is_empty():
				if sprites.has(pos):
					sprites[pos].queue_free()
					sprites.erase(pos)
			else:
				active_positions.append(pos)
				var type = data["type"]
				var color = data["color"]
				var piece_tex = piece_texture(color, type)
				if sprites.has(pos):
					sprites[pos].texture = piece_tex
				else:
					var s = Sprite2D.new()
					s.texture = piece_tex
					s.position = grid_to_pixel(Vector2(x, y))
					$Pieces.add_child(s)
					sprites[pos] = s
				sprites[pos].position = grid_to_pixel(Vector2(x, y))

# --- AI Move Logic (Threaded) ---

func make_next_ai_move():
	"""Trigger the next AI move based on whose turn it is."""
	if board.is_game_over() or game_finished:
		return

	# Prevent starting multiple threads
	if ai_thinking:
		return

	current_thinking_color = board.get_turn()
	var color_name = "White" if current_thinking_color == 0 else "Black"

	ai_thinking = true
	thinking_label.text = "%s thinking..." % color_name
	thinking_label.visible = true
	status_label.text = "Move %d - %s's turn" % [move_count + 1, color_name]

	# Clean up any existing thread
	cleanup_thread()

	# Create and start the thread
	ai_thread = Thread.new()
	ai_thread.start(_threaded_ai_search.bind(AI_DEPTH, current_thinking_color))

func _threaded_ai_search(max_depth: int, color: int):
	"""Runs on a separate thread to calculate the best move."""
	var color_name = "White" if color == 0 else "Black"
	print("\n%s Agent Thinking..." % color_name)
	var start_time = Time.get_ticks_msec()

	# Select the appropriate agent based on color
	var current_agent = white_agent if color == 0 else black_agent
	var best_move = current_agent.run_iterative_deepening(max_depth)

	var elapsed = Time.get_ticks_msec() - start_time

	# Report the depth actually reached
	var actual_depth = best_move.get("depth", 0)
	print("%s Agent completed search to depth %d in %d ms" % [color_name, actual_depth, elapsed])

	# Hand the result back to the main thread
	call_deferred("_on_ai_search_complete", best_move, color)

func _on_ai_search_complete(best_move: Dictionary, color: int):
	"""Handles the AI move result on the main thread."""

	# Wait for the thread to finish cleanly
	if ai_thread != null:
		ai_thread.wait_to_finish()
		ai_thread = null

	thinking_label.visible = false
	ai_thinking = false

	var color_name = "White" if color == 0 else "Black"

	if best_move.is_empty():
		print("No legal moves for %s!" % color_name)
		check_game_over()
		return

	var from_square: int = best_move["from"]
	var to_square: int = best_move["to"]
	var score: int = best_move["score"]
	var depth: int = best_move.get("depth", 0)

	var from_grid = square_to_grid(from_square)
	var to_grid = square_to_grid(to_square)

	var from_alg = board.square_to_algebraic(from_square)
	var to_alg = board.square_to_algebraic(to_square)
	print("%s plays: %s%s (score: %d, depth: %d)" % [color_name, from_alg, to_alg, score, depth])

	# Apply the move on the main thread
	board.make_move(from_square, to_square)

	update_last_move_visuals(from_grid, to_grid)
	refresh_visuals()
	move_count += 1

	# Train on the position if in training mode
	if TRAINING_MODE > 0 and (move_count % TRAIN_EVERY_N_MOVES == 0):
		train_agents_on_current_position()

	var game_over = check_game_over()

	if not game_over:
		# Continue to next move
		call_deferred("make_next_ai_move")
	else:
		# Game is over
		game_finished = true
		if TRAINING_MODE > 0:
			finalize_training()
			if SAVE_MODELS_AFTER_GAME:
				save_trained_models()

func cleanup_thread():
	if ai_thread != null:
		if ai_thread.is_started():
			ai_thread.wait_to_finish()
		ai_thread = null

func _exit_tree():
	# Ensure thread is cleaned up if scene is changed/quit
	cleanup_thread()

# --- Game State Management ---

func check_game_over() -> bool:
	"""Check if the game is over and report the result."""
	if board.is_game_over():
		var result = board.get_game_result()
		match result:
			1:
				print("\n=== GAME OVER ===")
				print("Checkmate! White wins!")
				status_label.text = "Game Over - White wins!"
			2:
				print("\n=== GAME OVER ===")
				print("Checkmate! Black wins!")
				status_label.text = "Game Over - Black wins!"
			3:
				print("\n=== GAME OVER ===")
				print("Draw / Stalemate!")
				status_label.text = "Game Over - Draw!"

		print("Final FEN: %s" % board.get_fen())
		print("Total moves: %d" % move_count)
		return true
	return false

# --- Training & Model Management ---

func train_agents_on_current_position():
	"""Train both agents on the current position based on training mode."""
	if TRAINING_MODE == 0:
		return

	var white_loss = 0.0
	var black_loss = 0.0

	if TRAINING_MODE == 1:
		# Heuristic Training: Use material evaluation as supervision
		white_loss = train_heuristic(white_agent, COLOR_WHITE)
		black_loss = train_heuristic(black_agent, COLOR_BLACK)

	elif TRAINING_MODE == 2:
		# Distillation Training: Use tree search evaluation as supervision
		white_loss = train_distillation(white_agent, COLOR_WHITE)
		black_loss = train_distillation(black_agent, COLOR_BLACK)

	# Track training statistics
	total_training_loss += white_loss + black_loss
	num_training_examples += 2

	# Print training progress every 10 moves
	if move_count % 10 == 0:
		var avg_loss = total_training_loss / num_training_examples if num_training_examples > 0 else 0.0
		var mode_name = "Heuristic" if TRAINING_MODE == 1 else "Distillation"
		print("  [%s Training] Move %d | Avg Loss: %.6f | White Loss: %.6f | Black Loss: %.6f" %
			  [mode_name, move_count, avg_loss, white_loss, black_loss])

func train_heuristic(agent: Agent, color: int) -> float:
	"""Train agent using material evaluation as target (heuristic training)."""
	# Simply delegate to the agent's built-in method
	return agent.train_on_current_position(color, LEARNING_RATE)

func train_distillation(agent: Agent, color: int) -> float:
	"""Train agent using tree search evaluation as target (distillation training)."""

	# 1. Extract features from current position
	var features = agent.get_features_for_color(color)

	# 2. Perform shallow tree search to get "teacher" evaluation
	# This search uses the neural network + minimax to get a better evaluation
	var search_result = agent.run_iterative_deepening(DISTILLATION_SEARCH_DEPTH)

	if search_result.is_empty():
		return 0.0

	# 3. Get the evaluation score from the search
	var search_score = search_result.get("score", 0)

	# 4. Convert the search score to a training target (0.0 to 1.0)
	var target = agent.score_to_target(search_score)

	# 5. Train the network to match this target
	# This teaches the network to "distill" the knowledge from tree search
	var loss = agent.train_single_example(features, target, LEARNING_RATE)

	return loss

func finalize_training():
	"""Called at the end of the game to finalize training."""
	if TRAINING_MODE == 0:
		return

	var avg_loss = total_training_loss / num_training_examples if num_training_examples > 0 else 0.0
	var mode_names = ["", "Heuristic", "Distillation"]

	print("\n=== TRAINING SUMMARY ===")
	print("Training Mode: %s" % mode_names[TRAINING_MODE])
	print("Total positions trained: %d" % num_training_examples)
	print("Average loss: %.6f" % avg_loss)
	print("Final material evaluation:")
	print("  White's perspective: %d centipawns" % white_agent.evaluate_material())
	print("  Black's perspective: %d centipawns" % (-white_agent.evaluate_material()))

	if TRAINING_MODE == 2:
		print("\nDistillation Training Info:")
		print("  Search depth used: %d" % DISTILLATION_SEARCH_DEPTH)
		print("  This amplified the network's knowledge through tree search")

func save_trained_models():
	"""Save both agent models after training."""
	print("\nSaving trained models...")

	var white_saved = white_agent.save_network(MODEL_WHITE_PATH)
	if white_saved:
		print("White agent saved to: %s" % MODEL_WHITE_PATH)
	else:
		print("Failed to save White agent model!")

	var black_saved = black_agent.save_network(MODEL_BLACK_PATH)
	if black_saved:
		print("Black agent saved to: %s" % MODEL_BLACK_PATH)
	else:
		print("Failed to save Black agent model!")

	print("Model saving complete.")

# Utility functions
func grid_to_pixel(grid_pos): return grid_pos * TILE_SIZE + BOARD_OFFSET
func pixel_to_grid(pixel_pos): return Vector2i(floor(pixel_pos.x / TILE_SIZE), floor(pixel_pos.y / TILE_SIZE))
func is_on_board(pos): return pos.x >= 0 and pos.x < 8 and pos.y >= 0 and pos.y < 8
