# sketchbook
arduino applications

Clone this to be a/your sketchbook.

E.G.: 
* cd ~
* git clone https://github.com/980f/sketchbook 

git clone will not overwrite an existing directory, you can name your sketchbook something else or rename your present work's directory then copy the files into the checked out one.


Each application must have its own directory of the same name as the .ino file it includes.

Each app directory then links to only the files it needs from the shared directory as arduino insists on compiling everything it finds with the .ino. The bash script arduous (in arduino.git) helps with making the links. This only works on an OS with file links, windows file links use mklink so you may wish to write an arduous.cmd using that.

The shared directory might someday be a git submodule, until then from sketchbook:
* git clone https://github.com/980f/arduino.git shared
* cd shared
* git clone https://github.com/980f/ezcpp

or run onclone.sh in sketchbook.

Those choices will be compatible with the links  you can organize differently but will have to redo your links. Please fork any repo you do that to.
To track links there is the script lardls (in arduino.git) which produces the script relib, and alters .gitignore. Add relib to your repo and run it after each git pull to get the links that project needs.
The support scripts can be added to a new arduino project via the script newsketch, in arduino.git.

(lardls: links for arduino lister, which I will probably rename now that it does more than that)
