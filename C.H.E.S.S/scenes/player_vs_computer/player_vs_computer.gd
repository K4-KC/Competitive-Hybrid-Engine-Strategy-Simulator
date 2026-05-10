extends Node2D

# Player vs Computer Mode
# Human plays as the starting color.
# The 'NeuralNet' agent observes the 'Board' state and calculates moves.

const TILE_SIZE = 16
const BOARD_OFFSET = Vector2(8, 8)

# AI Configuration
const AI_DEPTH = 5  # Maximum search depth for Iterative Deepening

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
var neural_net: NeuralNet # [NEW] The AI Agent
var sprites = {} # Map<Vector2i, Sprite2D>
var selected_pos = null # Vector2i (grid coordinates)

# Player/AI settings
var player_color: int = 0  # 0 = White, 1 = Black (set from FEN)
var ai_color: int = 1      # Computer plays the opposite color

# AI state
var ai_thinking: bool = false
var ai_thread: Thread = null # Thread for background calculation

# Temporary visual helpers
var highlight_sprites = []
var last_move_sprites = []

# Sprite atlas (board_sheet.png + board_sheet.json)
var slices_data = {}
var texture_cache = {}
var spritesheet_texture: Texture2D

# UI nodes and state
var promotion_panel = null
var promotion_buttons = {} 
var is_promoting = false
var pending_promotion_move_start = null 
var pending_promotion_move_end = null

# History for undo functionality
var fen_history = []

# AI thinking indicator
var thinking_label: Label = null

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

	# 2. [NEW] Initialize the AI Agent
	neural_net = NeuralNet.new()
	add_child(neural_net)
	# 3. [NEW] Link the Agent to the Board so it can see the pieces
	neural_net.set_board(board)
	# Optional: Load NN weights later with neural_net.load_network("res://ai/model.onnx")
	
	setup_ui()

	# Default to standard start if no FEN was passed
	var start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
	
	# Check if the Main Menu passed us a FEN string
	if get_tree().root.has_meta("start_fen"):
		start_fen = get_tree().root.get_meta("start_fen")
	
	board.setup_board(start_fen)
	
	# Determine player color from the starting position
	player_color = board.get_turn()
	ai_color = 1 - player_color
	
	fen_history.append(board.get_fen())
	refresh_visuals()
	
	var player_color_name = "White" if player_color == 0 else "Black"
	var ai_color_name = "White" if ai_color == 0 else "Black"
	print("Player vs Computer Mode Ready.")
	print("Player: %s | Computer: %s" % [player_color_name, ai_color_name])
	print("AI Max Depth: %d (Iterative Deepening via NeuralNet Agent)" % AI_DEPTH)
	print(start_fen)
	
	# If it's the AI's turn to start
	if board.get_turn() == ai_color:
		call_deferred("make_ai_move")

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

	# Promotion panel
	promotion_panel = PanelContainer.new()
	promotion_panel.visible = false
	promotion_panel.anchors_preset = Control.PRESET_CENTER 
	promotion_panel.position = Vector2(10, 52) 
	canvas.add_child(promotion_panel)
	var hbox = HBoxContainer.new()
	promotion_panel.add_child(hbox)
	var options = ["q", "r", "b", "n"]
	for type in options:
		var btn = Button.new()
		btn.custom_minimum_size = Vector2(16, 16)
		btn.pressed.connect(self._on_promotion_selected.bind(type))
		hbox.add_child(btn)
		promotion_buttons[type] = btn
	
	# AI thinking indicator label
	thinking_label = Label.new()
	thinking_label.text = "Computer thinking..."
	thinking_label.visible = false
	thinking_label.position = Vector2(10, 140)
	thinking_label.add_theme_font_size_override("font_size", 8)
	canvas.add_child(thinking_label)

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

func get_valid_moves_for_piece(grid_pos: Vector2i) -> Array:
	var square = grid_to_square(grid_pos)
	var move_squares = board.get_legal_moves_for_piece(square)
	var result = []
	for target_square in move_squares:
		result.append(square_to_grid(target_square))
	return result

# --- Input & Logic ---

func _input(event):
	# Block input during promotion or AI thinking
	if is_promoting or ai_thinking:
		return
	
	# Block input if it's the AI's turn
	if board.get_turn() == ai_color:
		return
	
	if event is InputEventKey and event.pressed and event.keycode == KEY_LEFT:
		revert_last_move()
		return

	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var clicked_pos = pixel_to_grid(event.position)
		if not is_on_board(clicked_pos): return

		if selected_pos != null:
			if clicked_pos == selected_pos:
				deselect_piece()
			else:
				var valid_targets = get_valid_moves_for_piece(selected_pos)
				
				if clicked_pos in valid_targets:
					var move_start = selected_pos
					var move_end = clicked_pos
					var start_square = grid_to_square(selected_pos)
					var end_square = grid_to_square(clicked_pos)
					
					var result = board.attempt_move(start_square, end_square)
					if result == 1:
						# Successful move
						update_last_move_visuals(move_start, move_end)
						deselect_piece()
						refresh_visuals()
						record_fen()
						check_game_over()
						
						# Trigger AI move
						if not board.is_game_over():
							make_ai_move()
					elif result == 2:
						# Promotion pending
						pending_promotion_move_start = move_start
						pending_promotion_move_end = move_end
						refresh_visuals()
						
						var piece_data = get_data_at(move_start)
						start_promotion(piece_data)
				else:
					# Clicked invalid target; check if selecting friendly piece
					var p = get_data_at(clicked_pos)
					if not p.is_empty() and p.color == player_color:
						select_piece(clicked_pos)
					else:
						deselect_piece()
		else:
			var p = get_data_at(clicked_pos)
			# Only allow selecting pieces of the player's color
			if not p.is_empty() and p.color == player_color:
				select_piece(clicked_pos)

func select_piece(pos: Vector2i):
	deselect_piece()
	selected_pos = pos
	spawn_highlight(get_slice_texture("selected"), pos)

	var valid_moves = get_valid_moves_for_piece(pos)
	for target in valid_moves:
		var target_data = get_data_at(target)
		var is_capture = not target_data.is_empty()

		# En passant check
		if target_data.is_empty():
			var piece_data = get_data_at(pos)
			if piece_data.has("type") and piece_data.type == "p":
				if target.x != pos.x:
					is_capture = true

		if is_capture:
			spawn_highlight(get_slice_texture("capture"), target)
		else:
			spawn_highlight(get_slice_texture("move"), target)

func deselect_piece():
	selected_pos = null
	clear_temp_highlights()

func spawn_highlight(texture, grid_pos):
	var s = Sprite2D.new()
	s.texture = texture
	s.position = grid_to_pixel(Vector2(grid_pos.x, grid_pos.y))
	$Highlights.add_child(s)
	highlight_sprites.append(s)

func clear_temp_highlights():
	for s in highlight_sprites:
		s.queue_free()
	highlight_sprites.clear()

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

func start_promotion(piece_data):
	is_promoting = true
	var color = piece_data["color"]
	for type in promotion_buttons:
		promotion_buttons[type].icon = piece_texture(color, type)
	promotion_panel.visible = true
	clear_temp_highlights()

func _on_promotion_selected(type):
	board.commit_promotion(type)
	promotion_panel.visible = false
	is_promoting = false
	
	if pending_promotion_move_start != null and pending_promotion_move_end != null:
		update_last_move_visuals(pending_promotion_move_start, pending_promotion_move_end)
		pending_promotion_move_start = null
		pending_promotion_move_end = null
	
	deselect_piece()
	refresh_visuals()
	record_fen()
	check_game_over()
	
	# Trigger AI move after player promotion
	if not board.is_game_over():
		make_ai_move()

# --- AI Move Logic (Threaded with NeuralNet Agent) ---

func make_ai_move():
	if board.is_game_over():
		return
	
	if board.get_turn() != ai_color:
		return
		
	# Prevent starting multiple threads
	if ai_thinking:
		return
	
	ai_thinking = true
	thinking_label.visible = true
	
	# Clean up any existing thread (sanity check)
	cleanup_thread()
	
	# Create and start the thread
	ai_thread = Thread.new()
	# Pass the max depth to the thread function
	ai_thread.start(_threaded_ai_search.bind(AI_DEPTH))

func _threaded_ai_search(max_depth: int):
	# This function runs on a separate thread
	print("\nAgent Thinking (NeuralNet + Iterative Deepening)...")
	var start_time = Time.get_ticks_msec()
	
	# [CHANGED] Call the search function on the NeuralNet AGENT, not the Board.
	# The agent handles the algorithms, heuristics, and evaluation.
	var best_move = neural_net.run_iterative_deepening(max_depth)
	
	var elapsed = Time.get_ticks_msec() - start_time
	
	# Report the depth actually reached
	var actual_depth = best_move.get("depth", 0)
	print("Agent completed search to depth %d in %d ms" % [actual_depth, elapsed])
	
	# Hand the result back to the main thread safely
	call_deferred("_on_ai_search_complete", best_move)

func _on_ai_search_complete(best_move: Dictionary):
	# This function runs on the main thread
	
	# Wait for the thread to finish cleanly
	if ai_thread != null:
		ai_thread.wait_to_finish()
		ai_thread = null
	
	thinking_label.visible = false
	ai_thinking = false
	
	if best_move.is_empty():
		print("No legal moves for computer!")
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
	print("Computer plays: %s%s (score: %d, depth: %d)" % [from_alg, to_alg, score, depth])
	
	# Apply the move on the main thread (state update)
	board.make_move(from_square, to_square)
	
	update_last_move_visuals(from_grid, to_grid)
	refresh_visuals()
	record_fen()
	check_game_over()

func cleanup_thread():
	if ai_thread != null:
		if ai_thread.is_started():
			ai_thread.wait_to_finish()
		ai_thread = null

func _exit_tree():
	# Ensure thread is cleaned up if scene is changed/quit
	cleanup_thread()

# --- Undo / History ---

func revert_last_move():
	# Block undo while AI is thinking
	if ai_thinking:
		return

	# In PvC mode, we revert TWO moves (player + AI) to get back to player's turn
	var moves = board.get_moves()
	if moves.size() == 0:
		return 
	
	# Revert AI's move
	board.revert_move()
	
	# Check if there's another move to revert (player's previous move)
	moves = board.get_moves()
	if moves.size() > 0:
		board.revert_move()
	
	deselect_piece()
	
	# Update FEN history
	while fen_history.size() > 1:
		var current_fen = board.get_fen()
		if fen_history[fen_history.size() - 1] != current_fen:
			fen_history.pop_back()
		else:
			break
	
	for s in last_move_sprites:
		s.queue_free()
	last_move_sprites.clear()
	
	var remaining_moves = board.get_moves()
	if remaining_moves.size() > 0:
		var last_uci = remaining_moves[remaining_moves.size() - 1]
		var prev_move = parse_uci_move(last_uci)
		if prev_move.size() == 2:
			update_last_move_visuals(prev_move[0], prev_move[1])
	
	refresh_visuals()
	print("\nUndid last moves. Current position:")
	print(board.get_fen())

func parse_uci_move(uci: String) -> Array:
	if uci.length() < 4: return []
	var from_file = uci.unicode_at(0) - "a".unicode_at(0)
	var from_rank = uci.unicode_at(1) - "1".unicode_at(0)
	var to_file = uci.unicode_at(2) - "a".unicode_at(0)
	var to_rank = uci.unicode_at(3) - "1".unicode_at(0)
	return [Vector2i(from_file, 7 - from_rank), Vector2i(to_file, 7 - to_rank)]

func record_fen():
	var fen = board.get_fen()
	fen_history.append(fen)
	print(fen)

func check_game_over():
	if board.is_game_over():
		var result = board.get_game_result()
		match result:
			1: 
				print("Game Over: Checkmate! White wins!")
				if player_color == 0:
					print("Congratulations! You won!")
				else:
					print("The computer wins!")
			2: 
				print("Game Over: Checkmate! Black wins!")
				if player_color == 1:
					print("Congratulations! You won!")
				else:
					print("The computer wins!")
			3: 
				print("Game Over: Draw / Stalemate!")

# Utility functions
func grid_to_pixel(grid_pos): return grid_pos * TILE_SIZE + BOARD_OFFSET
func pixel_to_grid(pixel_pos): return Vector2i(floor(pixel_pos.x / TILE_SIZE), floor(pixel_pos.y / TILE_SIZE))
func is_on_board(pos): return pos.x >= 0 and pos.x < 8 and pos.y >= 0 and pos.y < 8
