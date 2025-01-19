#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    #kernel build steps here
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j 4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
fi

echo "Adding the Image in outdir"
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

#Create necessary base directories
mkdir "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log var/run


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # Configure busybox
    make distclean 
    make defconfig
    

else
    cd busybox
fi

# Make and install busybox
cd "$OUTDIR/busybox"
make  ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make  CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install 

echo "Library dependencies"
cd "$OUTDIR/rootfs"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"

#Program interpreter library
programInterpreter=$(${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter" | grep -o -E '/.*.so.[0-9]' | xargs basename)

# save toolchain dir
toolchainBaseDir=$(type -a -P ${CROSS_COMPILE}gcc | xargs dirname | xargs dirname)
echo "Tool chain base dir:${toolchainBaseDir}"

#save all needed libraries
libraries=$(${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library" | grep -oE [0-9a-zA-Z_]+.so.?[0-9]*)

#determine bitness of the builded busybox
#There is no file command in the test container so I have decided to harcode bitness
#bitness=$(file bin/busybox | grep -o [0-9][0-9]-bit)

#chose right dir for libraries
libdir="lib64"
#Add library dependencies to rootfs
find -L $toolchainBaseDir -name "$programInterpreter" | xargs -t --replace cp {} "${OUTDIR}/rootfs/lib" # we must copy interpreter shared library into lib dir in any case ...

for library in $libraries
do
    find -L $toolchainBaseDir -name "$library" | xargs -t --replace cp {} "${OUTDIR}/rootfs/${libdir}"
done


# Make device nodes
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1
sudo chown -R root:root *
# Clean and build the writer utility

echo ${FINDER_APP_DIR}
cd ${FINDER_APP_DIR}
make  clean CROSS_COMPILE=${CROSS_COMPILE}
make  all CROSS_COMPILE=${CROSS_COMPILE}

# Copy the finder related scripts and executables to the /home directory
# sudo cp -r ${FINDER_APP_DIR} home
# on the target rootfs simplified memory
sudo cp autorun-qemu.sh "${OUTDIR}/rootfs/home"
sudo cp writer "${OUTDIR}/rootfs/home"
sudo cp writer.sh "${OUTDIR}/rootfs/home"
sudo cp finder.sh "${OUTDIR}/rootfs/home"
sudo cp finder-test.sh "${OUTDIR}/rootfs/home"
sudo mkdir "${OUTDIR}/rootfs/home/conf"
sudo cp ./conf/assignment.txt "${OUTDIR}/rootfs/home/conf"
sudo cp ./conf/username.txt "${OUTDIR}/rootfs/home/conf"
# Chown the root directory
cd "${OUTDIR}/rootfs"
sudo chown -R root:root *
# TODO: Create initramfs.cpio.gz
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ${OUTDIR}
gzip -f initramfs.cpio