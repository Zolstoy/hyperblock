extends AspectRatioContainer

enum UiState {WELCOME, AUTHENTICATING, PLAYING}
enum NetworkState {IDLE, CONNECTING, AUTHENTICATING, WAITING_DATA, CONNECT_TIMEOUT}

var ui_state: UiState = UiState.WELCOME
var network_state: NetworkState = NetworkState.IDLE

var socket: WebSocketPeer

var auth_hash = {
	"Value0": {
		"nickname": "",
		"password": ""
	}
}

var main_container: AspectRatioContainer

var welcome_page: Control
var login_lineedit: LineEdit
var play_button: Button

var auth_page: Control
var auth_status: RichTextLabel

var connect_timer: float = 0

func _ready() -> void:
	main_container = get_tree().get_first_node_in_group("main_container")

	welcome_page = get_tree().get_first_node_in_group("welcome_page")
	login_lineedit = get_tree().get_first_node_in_group("login_lineedit")
	play_button = get_tree().get_first_node_in_group("play_button")

	auth_page = get_tree().get_first_node_in_group("auth_page")
	auth_status = get_tree().get_first_node_in_group("auth_status")

	if !main_container or !welcome_page or !login_lineedit or !play_button or !auth_page or !auth_status:
		printerr("Some nodes were not found at UI init")
		get_tree().quit(1)
		return

	play_button.pressed.connect(on_play_button_pressed)

func _process(delta: float) -> void:
	var screen_size = get_viewport().get_visible_rect().size
	main_container.set_ratio(screen_size.x / screen_size.y)

	play_button.set_disabled(login_lineedit.text.is_empty())

	if network_state != NetworkState.IDLE:
		handle_network(delta)

func on_play_button_pressed():
	if login_lineedit.text.is_empty():
		print("ERROR: can't play: login empty")
		return ;

	transition_to(UiState.AUTHENTICATING)

func transition_to(next_ui_state: UiState):
	if ui_state == UiState.WELCOME && next_ui_state != UiState.WELCOME:
		welcome_page.visible = false

	if next_ui_state == UiState.WELCOME && ui_state != UiState.WELCOME:
		welcome_page.visible = true
		
	if ui_state == UiState.AUTHENTICATING && next_ui_state != UiState.AUTHENTICATING:
		auth_page.visible = false

	if next_ui_state == UiState.AUTHENTICATING && ui_state != UiState.AUTHENTICATING:
		auth_page.visible = true
		connect_to_server("127.0.0.1", 4567)

	ui_state = next_ui_state

		
func connect_to_server(hostname: String, port: int) -> bool:
	socket = WebSocketPeer.new()
	
	var options = TLSOptions.client()
	var cert = X509Certificate.new()
	if cert.load("ca_cert.pem") == OK:
		options = TLSOptions.client(cert)
	
	var url = "wss://" + hostname + ":%d" % port
	auth_status.set_text("Connecting to '%s'..." % url)
	print("Connecting to %s" % url)
	if socket.connect_to_url(url, options) != OK:
		return false
	network_state = NetworkState.CONNECTING
	return true
	

func handle_network(delta: float):		
	socket.poll()
	
	var socket_state = socket.get_ready_state()
	
	if socket_state == WebSocketPeer.STATE_CONNECTING && network_state != NetworkState.CONNECT_TIMEOUT:
		connect_timer += delta
		if connect_timer >= 3:
			print("Socket connection timeout")
			socket.close()
			connect_timer = 0
			network_state = NetworkState.CONNECT_TIMEOUT

	elif socket_state == WebSocketPeer.STATE_CLOSING:
		pass

	elif socket_state == WebSocketPeer.STATE_CLOSED:
		network_state = NetworkState.IDLE
		transition_to(UiState.WELCOME)

	elif  socket_state == WebSocketPeer.STATE_OPEN:
		if network_state == NetworkState.CONNECTING:
			network_state = NetworkState.AUTHENTICATING
			auth_hash.Value0.nickname = login_lineedit.text
			auth_hash.Value0.password = "secret123"
			socket.send_text(JSON.stringify(auth_hash))
		elif network_state == NetworkState.AUTHENTICATING:
			print("Authenticated?")
			network_state = NetworkState.WAITING_DATA

		elif network_state == NetworkState.WAITING_DATA:
			while socket.get_available_packet_count():
				var variant = JSON.parse_string(socket.get_packet().get_string_from_utf8())
				print("Received: %s" % variant)
