#!/bin/bash

# This is a special adaptation of build-fix.sh for DDK version.
#
# Usage: ./build_fix.sh [5.5|6.0]
#
# This script will generate the following output files:
#   nvme.sc
#

# Converting the first argument to all lower case for checking against the
# supporting vendor stings
VMKAPIDDK_VERSION=$(echo $1| tr '[:upper:]' '[:lower:]')

echo "Updating the nvme.sc file with the required DDK version"
if [ "$VMKAPIDDK_VERSION" = "5.5" ]
then
   # Replacing the VMKAPIDDK_VERSION_TAG with the appropriate value in nvme.sctmp
   # and creating the nvme.sc
   sed -i "s@VMKAPIDDK_VERSION=...@VMKAPIDDK_VERSION=550@g" nvme.sc
   echo "VMKAPIDDK VERSION: 550"
else
   sed -i "s@VMKAPIDDK_VERSION=...@VMKAPIDDK_VERSION=600@g" nvme.sc
   echo "VMKAPIDDK VERSION: 600"
fi

echo "Done."
