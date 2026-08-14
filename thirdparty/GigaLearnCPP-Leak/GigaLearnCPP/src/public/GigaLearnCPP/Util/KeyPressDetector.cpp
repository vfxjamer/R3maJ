#include "KeyPressDetector.h"

// R3maJ: keypress detector disabled - returns immediately (no blocking)
// Used only to disable interactive key reading during training/inference

char GGL::KeyPressDetector::GetPressedChar() {
	// R3maJ: no interactive key reads during rendering
	return 0;
}
