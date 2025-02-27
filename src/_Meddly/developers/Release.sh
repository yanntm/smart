#!/bin/bash
#
if [ $# -ne 0 ]; then
    printf "\nUsage: $0 \n\n"
    printf "Creates a release based on the version number in configure.ac.\n"
    printf "Automatically updates the revision date in the source,\n"
    printf "and creates a tag for the release, named v[version].\n\n"
    printf "Before running, be sure that all modified files have been committed\n"
    printf "to the repository.\n\n"
    exit 0
fi

version=`awk '/AC_INIT/{print $2}' ../configure.ac | tr -d '[],'`


printf "Creating release for version $version\n"

rdate=`date +"%Y %B %d"`
printf "const char* MEDDLY_DATE = \"%s\";\n" "$rdate" > ../src/revision.h

nextver=`awk -F. '{for (i=1; i<NF; i++) printf($i"."); print $NF+1}' <<< $version`

#
# Save changes
#

git add ../src/revision.h
git commit -m "Updated release date for $version"
git push
git push sourceforge

#
# Create and publish a tag for $version
#

git tag -a "v$version" -m "Tag for release $version"
git push origin v$version
git push sourceforge v$version


#
# Build distribution
#

make dist

#
# Automatically bump the version number
#

printf "Updating version to $nextver\n"
sed "/AC_INIT/s/$version/$nextver/" ../configure.ac > ../configure.new
mv -f ../configure.ac ../configure.ac.old
mv ../configure.new ../configure.ac

