BX71 single-photo gesture prototype
Build on the computer's Ubuntu WSL using the existing OpenCV ARM installation.
Run: bash /mnt/d/bx71_gesture_setup.txt
Output: D:\bx71_gesture_photo (ARM executable, not a Windows application).
Copy executable into TF boot partition's bx71_model_check folder.
On board, copy to /tmp, chmod +x, then run with palm model, hand model,
input JPEG and output JPEG arguments, as with bx71_photo_run.
FIST=1, INDEX=2, V_SIGN=3, OPEN_PALM=0. UNKNOWN produces no command.
FINGERS reports index/middle/ring/little as 1 extended, 0 folded, ? ambiguous.
The thumb is ignored for fist/index/V; open palm requires thumb extension.
COMMAND is printed only. This prototype does not drive GPIO or send UART.
This is a heuristic 2D classifier, not a trained gesture classifier.
Oblique poses, occlusion and blur may cause UNKNOWN or mistakes.
No temporal confirmation or live camera loop in this version.
Do not interpret model confidence as gesture accuracy.
Local validation: synthetic classifier rotation/scale and invalid-input tests passed.
ARM compilation and real-photo four-gesture validation remain to be done on the user's system.
