extends Node

const BASE_SIZE := Vector2i(512, 512)

var _last_screen := -1
var _last_scale := -1.0

func _ready() -> void:
	var win := get_window()
	win.content_scale_size = BASE_SIZE
	win.content_scale_mode = Window.CONTENT_SCALE_MODE_CANVAS_ITEMS
	win.content_scale_aspect = Window.CONTENT_SCALE_ASPECT_KEEP
	win.content_scale_stretch = Window.CONTENT_SCALE_STRETCH_INTEGER
	_apply(true)

func _process(_delta: float) -> void:
	_apply(false)

func _apply(initial: bool) -> void:
	var screen := DisplayServer.window_get_current_screen()
	var scale := DisplayServer.screen_get_scale(screen)
	if not initial and screen == _last_screen and is_equal_approx(scale, _last_scale):
		return
	_last_screen = screen
	_last_scale = scale
	var new_size := Vector2i(Vector2(BASE_SIZE) * scale)
	var win := get_window()
	win.size = new_size
	if initial:
		var screen_pos := DisplayServer.screen_get_position(screen)
		var screen_size := DisplayServer.screen_get_size(screen)
		win.position = screen_pos + (screen_size - new_size) / 2
