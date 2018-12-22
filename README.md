# sketchbook
arduino applications

Clone this to be a/your sketchbook.

E.G.: 
* cd ~
* git clone https://github.com/980f/sketchbook 

git clone will not overwrite an existing directory, you can name your sketchbook something else or rename your present work's directory then copy the files into the checked out one.


Each application must have its own directory of the same name as the .ino file it includes.

Each app directory then links to only the files it needs from the shared directory as arduino insists on compiling everything it finds with the .ino. bash script arduous helps with making the links. This only works on an OS with file links, windows file links use mklink so you may wish to write and arduous.cmd using that.

The shared directory might someday be a git submodule, until then from sketchbook:
* git clone https://github.com/980f/arduinio.git shared
* cd shared
* git clone https://github.com/980f/ezcpp

Those choices will be compatible with the links, you can organize differently but will have to redo your links. Please fork any repo you do that to.
To track links there is the script lardls which produces the file .lard Add .lard to your repo and you will have the list that you linked. I will get around to making a script that will read .lard and generate the links, until then you may choose to add the links to git making sure that you add them as links, not the linked file (I am not sure how to do that).
