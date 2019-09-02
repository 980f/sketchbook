# when you clone my arduino sketchbook you need to also clone my arduino shared libraries.
# execute this from the sketchbook directory, or do its steps elsewhere and add a symlink in the sketchbook to where the checkout is.
git clone https://github.com/980f/arduino.git shared
pushd shared
./onclone.sh
popd
