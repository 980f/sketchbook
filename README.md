# sketchbook
arduino applications

Clone this to be a/your sketchbook.

E.G.: 
* cd ~
* git clone https://github.com/980f/sketchbook 
git clone will not overwrite an existing directory, you can name your sketchbook something else or rename your preseent work's directory then copy the files into the checked out one.


Each application must have its own directory of the same name as the .ino file it includes.

Each app directory then links to only the files it needs from the shared directory as arduino insists on compiling everything it finds there in. bash script arduous helps with making the links. this only works on an OS with file links.

The shared directory might someday be a git submodule, until then from sketchbook:
* git clone https://github.com/980f/arduinio.git shared
* cd shared
* git clone https://github.com/980f/ezcpp

Those choices will be compatible with the links, you can organize differently but will have to redo your links. Please fork any repo you do that to.