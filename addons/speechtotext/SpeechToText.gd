tool
extends Node

var speech_to_text
    
func init():
    speech_to_text = load("res://addons/speechtotext/speechtotext.gdns").new()

func can_speak():
    return speech_to_text.ready()
    
func am_speaking():
    return speech_to_text.listening()

func start():
    speech_to_text.start()

func stop_and_get_result():
    speech_to_text.stop()
    while not speech_to_text.recognition_finished():
        yield(get_tree(), "idle_frame")
    return speech_to_text.get_result()
