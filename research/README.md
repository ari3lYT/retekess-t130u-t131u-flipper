# Research programs and scripts

The source tree preserves the investigation's probe programs and analysis methods. Historical raw captures and embedded captured-frame headers have been withheld because some contained the owner's speech. A few old probe applications therefore require a new header or fresh input capture to build or run. The integrated `../app/` is self-contained and uses newly generated silence payloads.

To reproduce the methods, collect your own T130U/T131U captures and adapt paths in the scripts. `make_silent_control_frames.py` generates the privacy-safe frames used by the application. `retekess_audio_encoder.py` builds CH19 audio radio frames without a captured template; this new encoder awaits physical validation.
