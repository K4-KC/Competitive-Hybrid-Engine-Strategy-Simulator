extends TextureRect

# --- Grid settings ---
const TILE_SIZE = 16
const BOARD_OFFSET = Vector2(8, 8)

# --- Piece type constants ---
const PIECE_NONE = 0
const PIECE_PAWN = 1
const PIECE_KNIGHT = 2
const PIECE_BISHOP = 3
const PIECE_ROOK = 4
const PIECE_QUEEN = 5
const PIECE_KING = 6

# --- Color constants ---
const COLOR_WHITE = 8
const COLOR_BLACK = 16

# --- Masks ---
const PIECE_TYPE_MASK = 7
const COLOR_MASK = 24

var board: Board
var sprites = {}
var selected_pos = null

var highlight_sprites = []
var last_move_sprites = []

# Atlas (board_sheet.png + board_sheet.json)
var slices_data = {}
var texture_cache = {}
var spritesheet_texture: Texture2D

# UI
var promotion_panel = null
var promotion_buttons = {}
var is_promoting = false
var pending_promotion_move_start = null
var pending_promotion_move_end = null

var fen_history = []

func _ready():
	load_aseprite_assets("res://assets/sprites/board_sheet.json", "res://assets/sprites/board_sheet.png")

	var hl_node = Node2D.new()
	hl_node.name = "Highlights"
	add_child(hl_node)

	var pieces_node = Node2D.new()
	pieces_node.name = "Pieces"
	add_child(pieces_node)

	board = Board.new()
	add_child(board)

	setup_ui()

	var start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
	if get_tree().root.has_meta("start_fen"):
		start_fen = get_tree().root.get_meta("start_fen")

	board.setup_board(start_fen)
	fen_history.append(board.get_fen())
	refresh_visuals()

	print("PvP Mode Ready. White to move.")
	print(start_fen)

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

func is_christmas_season() -> bool:
	var date = Time.get_date_dict_from_system()
	var month = date["month"]
	var day = date["day"]
	return (month == 12 and day >= 1) or (month == 1 and day <= 6)

# --- UI ---

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
		btn.icon = get_slice_texture(type + "0")
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

# --- Input ---

func _input(event):
	if is_promoting: return

	if event is InputEventKey and event.pressed and event.keycode == KEY_LEFT:
		revert_last_move()
		return

	if event is InputEventMouseButton and event.pressed and event.button_index == MOUSE_BUTTON_LEFT:
		var local_mouse_pos = get_local_mouse_position()
		var clicked_pos = pixel_to_grid(local_mouse_pos)
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
						update_last_move_visuals(move_start, move_end)
						deselect_piece()
						refresh_visuals()
						record_fen()
						check_game_over()
					elif result == 2:
						pending_promotion_move_start = move_start
						pending_promotion_move_end = move_end
						refresh_visuals()
						var piece_data = get_data_at(move_start)
						start_promotion(piece_data)
				else:
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
	spawn_highlight(get_slice_texture("selected"), pos)

	var piece_data = get_data_at(pos)
	var aura = ""
	match piece_data.type:
		"p": aura = "p"
		"n": aura = "n"
		"b": aura = "b"
		"r": aura = "r"
		"q": aura = "q"
		"k":
			aura = "k_santa" if is_christmas_season() else "k"
	spawn_highlight(get_slice_texture(aura + "_aura"), pos)

	var valid_moves = get_valid_moves_for_piece(pos)
	for target in valid_moves:
		var target_data = get_data_at(target)
		var is_capture = not target_data.is_empty()

		if target_data.is_empty():
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

func spawn_move_highlight(grid_pos: Vector2i):
	var s = AnimatedSprite2D.new()
	var frames = SpriteFrames.new()
	frames.add_animation("default")
	frames.set_animation_loop("default", true)
	frames.set_animation_speed("default", 2.0)
	frames.add_frame("default", get_slice_texture("moved"))
	frames.add_frame("default", get_slice_texture("moved_"))
	s.sprite_frames = frames
	s.play("default")
	s.centered = true
	s.position = grid_to_pixel(Vector2(grid_pos.x, grid_pos.y))
	$Highlights.add_child(s)
	last_move_sprites.append(s)

func spawn_highlight(highlight_texture, grid_pos):
	if highlight_texture == null: return
	var s = Sprite2D.new()
	s.texture = highlight_texture
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
	spawn_move_highlight(start)
	spawn_move_highlight(end)

func refresh_visuals():
	for x in range(8):
		for y in range(8):
			var pos = Vector2i(x, y)
			var data = get_data_at(pos)
			if data.is_empty():
				if sprites.has(pos):
					sprites[pos].queue_free()
					sprites.erase(pos)
			else:
				var type = data["type"]
				var color = data["color"]
				var slice_name = type + str(color)
				if type == "k" and is_christmas_season():
					slice_name = "k" + str(color) + "_santa"
				var tex = get_slice_texture(slice_name)
				if sprites.has(pos):
					sprites[pos].texture = tex
				else:
					var s = Sprite2D.new()
					s.texture = tex
					s.position = grid_to_pixel(Vector2(x, y))
					$Pieces.add_child(s)
					sprites[pos] = s
				sprites[pos].position = grid_to_pixel(Vector2(x, y))

func start_promotion(piece_data):
	is_promoting = true
	var color = piece_data["color"]
	for type in promotion_buttons:
		promotion_buttons[type].icon = get_slice_texture(type + str(color))
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

func grid_to_pixel(grid_pos):
	return grid_pos * TILE_SIZE + BOARD_OFFSET

func pixel_to_grid(pixel_pos):
	return Vector2i(floor(pixel_pos.x / TILE_SIZE), floor(pixel_pos.y / TILE_SIZE))

func is_on_board(pos):
	return pos.x >= 0 and pos.x < 8 and pos.y >= 0 and pos.y < 8
