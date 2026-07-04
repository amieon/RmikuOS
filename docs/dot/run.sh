#!/bin/bash
dot -Tpng ./arch.dot     -o ../images/arch.png
dot -Tpng ./process.dot  -o ../images/process.png
dot -Tpng ./vfs.dot      -o ../images/vfs.png
dot -Tpng ./memory.dot   -o ../images/memory.png
dot -Tpng ./gcn.dot      -o ../images/gcn.png
echo "Done"