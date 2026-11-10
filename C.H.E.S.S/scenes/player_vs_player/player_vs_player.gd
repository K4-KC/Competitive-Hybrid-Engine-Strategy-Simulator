extends Node2D

const TILE_SIZE = 64
const BOARD_OFFSET = Vector2(32, 32)
const SPRITE_SCALE = 4.0

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
var sprites = {} # Map<Vector2i, Sprite2D>
var selected_pos = null # Vector2i (grid coordinates)

# Temporary visual helpers
var highlight_sprites = []
var last_move_sprites = []

# Textures
var tex_move = preload("res://assets/sprites/highlights/move.png")
var tex_capture = preload("res://assets/sprites/highlights/capture.png")
var tex_moving = preload("res://assets/sprites/highlights/moving.png")
var tex_moved = preload("res://assets/sprites/highlights/moved.png")

func is_christmas_season() -> bool:
	var date = Time.get_date_dict_from_system()
	var month = date["month"]
	var day = date["day"]
	return (month == 12 and day >= 1) or (month == 1 and day <= 6)

var textures = {
	0: { "p": preload("res://assets/sprites/pieces/p0.png"), "r": preload("res://assets/sprites/pieces/r0.png"), "n": preload("res://assets/sprites/pieces/n0.png"),
		 "b": preload("res://assets/sprites/pieces/b0.png"), "q": preload("res://assets/sprites/pieces/q0.png"),
		 "k": preload("res://assets/sprites/pieces/k0_santa.png") if is_christmas_season() else preload("res://assets/sprites/pieces/k0.png") },
	1: { "p": preload("res://assets/sprites/pieces/p1.png"), "r": preload("res://assets/sprites/pieces/r1.png"), "n": preload("res://assets/sprites/pieces/n1.png"),
		 "b": preload("res://assets/sprites/pieces/b1.png"), "q": preload("res://assets/sprites/pieces/q1.png"),
		 "k": preload("res://assets/sprites/pieces/k1_santa.png") if is_christmas_season() else preload("res://assets/sprites/pieces/k1.png") }
}

# UI nodes and state
var promotion_panel = null
var promotion_buttons = {} 
var is_promoting = false
var pending_promotion_move_start = null 
var pending_promotion_move_end = null

# History for undo functionality
var fen_history = []

func _ready():
	var hl_node = Node2D.new()
	hl_node.name = "Highlights"
	add_child(hl_node)
	
	var pieces_node = Node2D.new()
	pieces_node.name = "Pieces"
	add_child(pieces_node)

	board = Board.new()
	add_child(board)
	
	setup_ui()

	# Default to standard start if no FEN was passed
	var start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
	
	# Check if the Main Menu passed us a FEN string
	if get_tree().root.has_meta("start_fen"):
		start_fen = get_tree().root.get_meta("start_fen")
	
	board.setup_board(start_fen)
	fen_history.append(board.get_fen())
	refresh_visuals()
	
	print("PvP Mode Ready. White to move.")
	print(start_fen)

func setup_ui():
	var canvas = CanvasLayer.new()
	add_child(canvas)
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
	if is_promoting: return
	
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
					if not p.is_empty() and p.color == board.get_turn():
						select_piece(clicked_pos)
					else:
						deselect_piece()
		else:
			var p = get_data_at(clicked_pos)
			if not p.is_empty() and p.color == board.get_turn():
				select_piece(clicked_pos)

func select_piece(pos: Vector2i):
	deselect_piece()
	selected_pos = pos
	spawn_highlight(tex_moving, pos)
	
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
			spawn_highlight(tex_capture, target)
		else:
			spawn_highlight(tex_move, target)

func deselect_piece():
	selected_pos = null
	clear_temp_highlights()

func spawn_highlight(texture, grid_pos):
	var s = Sprite2D.new()
	s.texture = texture
	s.scale = Vector2(SPRITE_SCALE, SPRITE_SCALE)
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

	var s1 = Sprite2D.new()
	s1.texture = tex_moved
	s1.scale = Vector2(SPRITE_SCALE, SPRITE_SCALE)
	s1.position = grid_to_pixel(Vector2(start.x, start.y))
	$Highlights.add_child(s1)
	last_move_sprites.append(s1)

	var s2 = Sprite2D.new()
	s2.texture = tex_moved
	s2.scale = Vector2(SPRITE_SCALE, SPRITE_SCALE)
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
				if sprites.has(pos):
					sprites[pos].texture = textures[color][type]
				else:
					var s = Sprite2D.new()
					s.texture = textures[color][type]
					s.scale = Vector2(SPRITE_SCALE, SPRITE_SCALE)
					s.position = grid_to_pixel(Vector2(x, y))
					$Pieces.add_child(s)
					sprites[pos] = s
				sprites[pos].position = grid_to_pixel(Vector2(x, y))

func start_promotion(piece_data):
	is_promoting = true
	var color = piece_data["color"]
	for type in promotion_buttons:
		promotion_buttons[type].icon = textures[color][type]
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

func revert_last_move():
	var moves = board.get_moves()
	if moves.size() == 0:
		return 
	
	board.revert_move()
	deselect_piece()
	
	if fen_history.size() > 1:
		fen_history.pop_back()
	
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
	print("\n\n" + "=".repeat(50) + "\n")
	for fen in fen_history:
		print(fen)

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
			1: print("Game Over: Checkmate! White wins!")
			2: print("Game Over: Checkmate! Black wins!")
			3: print("Game Over: Draw / Stalemate!")

# Utility functions
func grid_to_pixel(grid_pos): return grid_pos * TILE_SIZE + BOARD_OFFSET
func pixel_to_grid(pixel_pos): return Vector2i(floor(pixel_pos.x / TILE_SIZE), floor(pixel_pos.y / TILE_SIZE))
func is_on_board(pos): return pos.x >= 0 and pos.x < 8 and pos.y >= 0 and pos.y < 8
