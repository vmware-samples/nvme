#!/bin/bash

ARRAY=("$@")
ELEMENTS=${#ARRAY[@]}

###############################################################################
# Check user input. inbox or not
###############################################################################
# Converting the first argument to all lower case for checking against the
# supporting vendor stings
VENDOR=$(echo $1| tr '[:upper:]' '[:lower:]')

echo ""
###############################################################################
# Get the DDK path from symbolic link vmkapiddk
###############################################################################
# Hardcoding the VMKAPIDDK Path as an assumption 100% we will have
# symbolic link vmkapiddk to vmkapiddk-x.x.0-xxxxxxx
VMKAPI_DDK=/opt/vmware/vmkapiddk
VMKAPIDDK_PATH=$(readlink -f $VMKAPI_DDK)
echo "VMKAPIDDK_PATH = $VMKAPIDDK_PATH"

###############################################################################
# Parse the DDK name from DDK path
###############################################################################
set -- "$VMKAPIDDK_PATH"
IFS='/'; declare -a cwdArray=($*)
VMKAPIDDK_NAME=${cwdArray[3]}
echo "VMKAPIDDK_NAME = $VMKAPIDDK_NAME"

###############################################################################
# Parse the build number and DDK version from the DDK name.
###############################################################################
set -- "$VMKAPIDDK_NAME"
IFS="-"; declare -a vmkapiddkArray=($*)
BUILD_NUMBER=${vmkapiddkArray[2]}
echo "BUILD_NUMBER   = $BUILD_NUMBER"
DDK_VERSION=${vmkapiddkArray[1]}
echo "DDK_VERSION = $DDK_VERSION"

############################################################################
# Creating a Makefile from template
############################################################################
cp Makefile_nvme_esxi55_60.tmp Makefile

echo "Updating the Makefile with the actual build number ..."
############################################################################
# Replace & update BUILD_NUMBER with actual build number in Makefile template
############################################################################
sed -i "s/^BUILD_NUMBER = .*/BUILD_NUMBER = $BUILD_NUMBER/" Makefile

############################################################################
# If the DDK is 5.5.0 changing the PRODUCT to ddk, for 6.0.0 leaving it as
# vmkapiddk as that is default in template
############################################################################
if [ $DDK_VERSION == "5.5.0" ]; then
   sed -i "s/^PRODUCT      = .*/PRODUCT      = ddk/" Makefile
fi


echo ""
###############################################################################
# Add executable Permissions to scons and bora files.
###############################################################################
echo "Adding executable permission to all files in $VMKAPI_DDK/src/scons"
chmod -R +x "$VMKAPI_DDK"/src/scons
echo "Adding executable permission to all files in $VMKAPI_DDK/src/bora"
chmod -R +x "$VMKAPI_DDK"/src/bora

echo ""
###############################################################################
# Create symbolic link of working directory UNDER /opt/vmware/vmkapiddk/src/ .
###############################################################################
if [ -e $VMKAPI_DDK/src/vsphere-2015 ]; then
   echo "symbolic link for vsphere-2015 already exists in $VMKAPI_DDK/src/"
   ls -l $VMKAPI_DDK/src/vsphere-2015
   echo ""
   echo "removing the link."
   rm -rf $VMKAPI_DDK/src/vsphere-2015
fi
echo ""
echo "creating new symbolic link for vsphere-2015 in $VMKAPI_DDK/src/"
echo $(pwd)
ln -s "$(pwd)/../../" $VMKAPI_DDK/src/vsphere-2015
ls -l $VMKAPI_DDK/src/vsphere-2015

echo ""
###############################################################################
# Create nvme.sc file from sc file template.
###############################################################################
cp nvme_esxi55_60.sc.tmp nvme.sc

###############################################################################
# Changing the cc defs VMKAPIDDK_VERSION in sc file to the right version 
###############################################################################
if [ $DDK_VERSION == "5.5.0" ]; then
   sed -i "s/^                         \"VMKAPIDDK_VERSION=650\",.*/                         \"VMKAPIDDK_VERSION=550\",/" nvme.sc
elif [ $DDK_VERSION == "6.0.0" ]; then
   sed -i "s/^                         \"VMKAPIDDK_VERSION=650\",.*/                         \"VMKAPIDDK_VERSION=600\",/" nvme.sc
fi


for ((i=0;i<ELEMENTS;i++))
do
     if [ "${ARRAY[${i}]}" = "-d" ]; then
          let i=i+1
          echo -d="\"${ARRAY[${i}]}\""
          sed -i 's/^.*"VMK_DEVKIT_HAS_API_VMKAPI_MPP",/                         "'"${ARRAY[${i}]}"'",\n                         "VMK_DEVKIT_HAS_API_VMKAPI_MPP",/' nvme.sc

     fi
done

echo "Done."
