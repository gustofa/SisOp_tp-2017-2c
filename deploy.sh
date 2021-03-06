#!/bin/bash

export LC_ALL=C

# se obtiene el directorio desde donde se ejecuta el sh
SCRIPT=$(readlink -f $0);
dir_base=`dirname $SCRIPT`;

# se descargan scripts para datasets
	if [ -d "SO-Nombres-Dataset" ]; then rm -Rf SO-Nombres-Dataset; fi
	#mkdir SO-Nombres-Dataset
	#git clone https://github.com/iago64/SO-Nombres-Dataset.git
	curl -u 'charlos' -L -o lastp.tar https://github.com/iago64/SO-Nombres-Dataset/tarball/master
	tar -xvf $dir_base/lastp.tar
	#cd $dir_base/SO-Nombres-Dataset
	#sudo chmod 777 *.py
	#cd $dir_base

# se descarga y se instala so-commons
	if [ -d "so-commons-library" ]; then rm -Rf so-commons-library; fi
	git clone https://dromero-7854:ASDzxc7854@github.com/sisoputnfrba/so-commons-library.git ./so-commons-library
	cd ./so-commons-library
	make
	sudo make install
	#cd ..
	#rm -Rf so-commons-library;

# se descarga y se instala readline
	sudo apt-get install libreadline6 libreadline6-dev

# shared-library
	cd $dir_base/shared-library/shared-library
	make clean
	make all
	sudo cp libshared-library.so /usr/lib/

# master
	cd $dir_base/master/src
	make clean
	make all

# file-system
	cd $dir_base/filesystem/src
	make clean
	make all

# yama
	cd $dir_base/yama/src
	make clean
	make all

# worker
	cd $dir_base/worker/src
	make clean
	make all

# data-node
	cd $dir_base/datanode/src
	make clean
	make all
