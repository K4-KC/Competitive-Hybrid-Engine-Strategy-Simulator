extends Node2D

# Default Standard Chess FEN
const DEFAULT_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

const CUSTOM_FONT_PATH = "res://assets/fonts/SheepyK4.ttc"
const CUSTOM_FONT_SIZE = 8

# UI Components
var ui_layer: CanvasLayer
var pvp_btn: Button
var pvc_btn: Button
var perft_btn: Button
var fen_input: LineEdit
var play_btn: Button
var default_btn: Button

# State
var selected_mode = "PvP" # Options: "PvP", "PvC", "Perft"

func _ready():
	# Create a CanvasLayer to ensure UI stays on top and aligned
	ui_layer = CanvasLayer.new()
	add_child(ui_layer)
	# setup_main_menu_ui()

func _input(event):
	# Checks if a key was pressed and if that key is 'Q'
	if event is InputEventKey and event.pressed and event.keycode == KEY_Q:
		get_tree().quit()

func setup_main_menu_ui():
	# Setup Custom Theme
	var menu_theme = Theme.new()
	
	if ResourceLoader.exists(CUSTOM_FONT_PATH):
		var custom_font = load(CUSTOM_FONT_PATH)
		if custom_font:
			# Set the default font for all controls in this theme
			menu_theme.default_font = custom_font
			menu_theme.default_font_size = CUSTOM_FONT_SIZE
			
			print("Custom font loaded successfully: " + CUSTOM_FONT_PATH)
	else:
		print("Custom font not found at: " + CUSTOM_FONT_PATH + ". Using default font.")

	# Main Panel Container
	var panel = PanelContainer.new()
	panel.name = "MainPanel"
	panel.theme = menu_theme

	panel.texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	
	panel.anchors_preset = Control.PRESET_CENTER
	panel.custom_minimum_size = Vector2(85, 55)  # Slightly wider to accommodate 3 buttons
	
	# Center the panel
	panel.position = Vector2(
		(get_viewport_rect().size.x - panel.custom_minimum_size.x) / 2,
		(get_viewport_rect().size.y - panel.custom_minimum_size.y) / 2
	)
	
	ui_layer.add_child(panel)
	
	# Main Vertical Layout
	var vbox = VBoxContainer.new()
	# Add some margin/padding
	var margin = MarginContainer.new()
	margin.add_theme_constant_override("margin_top", 2)
	margin.add_theme_constant_override("margin_left", 2)
	margin.add_theme_constant_override("margin_right", 2)
	margin.add_theme_constant_override("margin_bottom", 2)
	margin.add_child(vbox)
	panel.add_child(margin)
	
	# Title
	var title = Label.new()
	title.text = "Chess Engine Game Mode"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vbox.add_child(title)
	vbox.add_child(HSeparator.new())
	
	# Mode Selection Buttons
	var mode_hbox = HBoxContainer.new()
	mode_hbox.alignment = BoxContainer.ALIGNMENT_CENTER
	mode_hbox.add_theme_constant_override("separation", 2)  # Add some spacing between buttons
	
	pvp_btn = Button.new()
	pvp_btn.text = "P Vs P"
	pvp_btn.focus_mode = Control.FOCUS_NONE
	pvp_btn.toggle_mode = true
	pvp_btn.button_pressed = true # Default
	pvp_btn.pressed.connect(_on_pvp_selected)
	pvp_btn.tooltip_text = "Player vs Player"
	
	pvc_btn = Button.new()
	pvc_btn.text = "P Vs C"
	pvc_btn.focus_mode = Control.FOCUS_NONE
	pvc_btn.toggle_mode = true
	pvc_btn.pressed.connect(_on_pvc_selected)
	pvc_btn.tooltip_text = "Player vs Computer"
	
	perft_btn = Button.new()
	perft_btn.text = "Perft"
	perft_btn.focus_mode = Control.FOCUS_NONE
	perft_btn.toggle_mode = true
	perft_btn.pressed.connect(_on_perft_selected)
	perft_btn.tooltip_text = "Performance Test Analysis"
	
	mode_hbox.add_child(pvp_btn)
	mode_hbox.add_child(pvc_btn)
	mode_hbox.add_child(perft_btn)
	vbox.add_child(mode_hbox)
	
	# Spacer
	vbox.add_child(Control.new())
	
	# FEN Input
	var fen_label = Label.new()
	fen_label.text = "Starting Position (FEN):"
	vbox.add_child(fen_label)
	
	fen_input = LineEdit.new()
	fen_input.text = ""
	# fen_input.focus_mode = Control.FOCUS_NONE
	fen_input.add_theme_stylebox_override("focus", StyleBoxEmpty.new())
	fen_input.placeholder_text = "Paste custom FEN here..."
	fen_input.custom_minimum_size.x = 100
	vbox.add_child(fen_input)
	
	# Bottom Controls (Default & Play)
	var bottom_hbox = HBoxContainer.new()
	bottom_hbox.add_theme_constant_override("separation", 5)
	
	# Default Button (Left side)
	default_btn = Button.new()
	default_btn.text = "Default"
	default_btn.focus_mode = Control.FOCUS_NONE
	default_btn.pressed.connect(_on_default_pressed)
	default_btn.tooltip_text = "Paste the default position"
	bottom_hbox.add_child(default_btn)
	
	# Spacer to push Play button to the right
	var spacer = Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bottom_hbox.add_child(spacer)
	
	# Play Button (Right side)
	play_btn = Button.new()
	play_btn.text = "Play"
	play_btn.focus_mode = Control.FOCUS_NONE
	play_btn.pressed.connect(_on_play_pressed)
	play_btn.custom_minimum_size.x = 20
	bottom_hbox.add_child(play_btn)
	
	vbox.add_child(HSeparator.new())
	vbox.add_child(bottom_hbox)
	
	_update_button_visuals()

func _on_pvp_selected():
	selected_mode = "PvP"
	_update_button_visuals()

func _on_pvc_selected():
	selected_mode = "PvC"
	_update_button_visuals()

func _on_perft_selected():
	selected_mode = "Perft"
	_update_button_visuals()

func _update_button_visuals():
	pvp_btn.set_pressed_no_signal(selected_mode == "PvP")
	pvc_btn.set_pressed_no_signal(selected_mode == "PvC")
	perft_btn.set_pressed_no_signal(selected_mode == "Perft")

func _on_default_pressed():
	fen_input.text = DEFAULT_FEN

func _on_play_pressed():
	var target_fen = fen_input.text.strip_edges()
	if target_fen.is_empty():
		target_fen = DEFAULT_FEN
	
	# Store the FEN in global metadata so the next scene can retrieve it
	get_tree().root.set_meta("start_fen", target_fen)
	
	var target_scene_path = ""
	
	match selected_mode:
		"PvP":
			target_scene_path = "res://scenes/player_vs_player.tscn"
		"PvC":
			target_scene_path = "res://scenes/player_vs_computer.tscn"
		"Perft":
			target_scene_path = "res://scenes/perft_analysis.tscn"
	
	# Verify file exists before changing to avoid crash
	if ResourceLoader.exists(target_scene_path):
		get_tree().change_scene_to_file(target_scene_path)
	else:
		print("Error: Scene file not found at " + target_scene_path)
		print("Please ensure you have saved the scene.")

# Handle window resize to keep panel centered
func _process(_delta):
	if ui_layer.get_child_count() > 0:
		var panel = ui_layer.get_child(0)
		if panel is PanelContainer:
			var scaled_size = panel.size * panel.scale
			panel.position = Vector2(
				(get_viewport_rect().size.x - scaled_size.x) / 2,
				(get_viewport_rect().size.y - scaled_size.y) / 2
			)
