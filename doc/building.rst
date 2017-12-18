.. _building:

====================
How to build libslax
====================

Building libslax invokes four steps:
- installing prerequisite software
- downloading software
- building libslax binaries
- installing libslax

Prerequisites
-------------

libslax, like any complex piece of software, depends on a number of
other software packages.  On many systems, these packages are already
installed, but some systems require manual installation of some
prerequisites.

Prerequisites for Cygwin
++++++++++++++++++++++++

Under category "Devel", install the packages:
- make
- gcc
- bison
- libxml2-devel

Under category "GNOME", install the package:

- libxslt-devel

Under category "Web" or "Net", install the package:

- libcurl-devel.

Prerequisites for Ubuntu
++++++++++++++++++++++++

Install the following packages:
 - libxml2-dev
 - libxslt-dev
 - libcurl-dev (for UB14: libcurl4-openssl-dev)
 - bison
 - autoconf
 - libtool

Prerequisites for MACOSX
++++++++++++++++++++++++

Install xcode development package:
  - https://developer.apple.com/xcode/
  - You'll also need to download the command line tools

Modern macosx includes versions of libxml2, libxslt, and libcurl
sufficient that you don't need to install the macports versions.  But
I install them anyway since macports tends to stay more current.  If
this DIY appeals to you:

Install MacPorts
  - http://www.macports.org/install.php

Using MacPorts, install the rest of the prereqs:
  - "port install gmake autoconf libxml2 libxslt curl"
  - install either libedit or readline

URLs for DIY
++++++++++++

libslax depends on the following software packages:

- libxml2
  - home: http://www.xmlsoft.org/index.html
  - download information: http://www.xmlsoft.org/downloads.html

- libxslt
  - home: http://xmlsoft.org/XSLT/
  - download information: http://xmlsoft.org/XSLT/downloads.html

- libcurl (optional)
  - home: http://curl.haxx.se/libcurl/
  - download information: http://curl.haxx.se/download.html

- libedit (optional)
  - note: libreadline can also be used optionally
  - home: http://www.thrysoee.dk/editline/

Retrieving the source code
--------------------------

[Note: for Solaris users, the "tar" command lacks the "-z" flag,
so you'll need to substitute "gzip -dc `file` | tar xf -" instead of
"tar -zxf `file`".]

You can retrieve the source for libslax in two ways:

A) Use a "distfile" for a specific release.  We use
github to maintain our releases.  Visit
[github release page](https://github.com/Juniper/libslax/releases)
to see the list of releases.  To download the latest, look for the
release with the green "Latest release" button and the green
"libslax-RELEASE.tar.gz" button under that section.

After downloading that release's distfile, untar it as follows::

    tar -zxf libslax-RELEASE.tar.gz
    cd libslax-RELEASE

B) Use the current build from github.  This gives you the most recent
source code, which might be less stable than a specific release.  To
build libslax from the git repo::

    git clone https://github.com/Juniper/libslax.git
    cd libslax

.. admonition: Be Aware
    The github repository does _not_ contain the files generated
    by "autoreconf", with the notable exception of the "m4" directory.
    Since these files (depcomp, configure, missing, install-sh, etc) are
    generated files, we keep them out of the source code repository.

    This means that if you download the a release distfile, these files
    will be ready and you'll just need to run "configure", but if you
    download the source code from svn, then you'll need to run
    "autoreconf" by hand.  This step is done for you by the "setup.sh"
    script, described in the next section.

Building libslax
----------------

To build libslax, you'll need to set up the build, run the `configure`
script, run the `make` command, and run the regression tests.

The following is a summary of the commands needed.  These commands are
explained in detail in the rest of this section::

    sh bin/setup.sh
    cd build
    ../configure
    make
    make test
    sudo make install

In the following sections, I'll walk thru each of these steps with
additional details and options, but the above directions should be all
that's needed.

Setting Up The Build
++++++++++++++++++++

[If you downloaded a distfile, you can skip this step.]

Run the "setup.sh" script to set up the build.  This script runs the
"autoreconf" command to generate the `configure` script and other
generated files::

    sh bin/setup.sh

Note: We're are currently using autoreconf version 2.69.

Running the `configure` Script
++++++++++++++++++++++++++++++

Configure (and autoconf in general) provides a means of building
software in diverse environments.  Our configure script supports
a set of options that can be used to adjust to your operating
environment. Use "configure --help" to view these options.

We use the "build" directory to keep object files and generated files
away from the source tree.

To run the configure script, change into the `build` directory, and
run the `configure` script.  Add any required options to the
"../configure" command line::

    % cd build
    % ../configure

Expect to see the `configure` script generate the following error::

    /usr/bin/rm: cannot remove `libtoolT': No such file or directory

This error is harmless and can be safely ignored.

By default, libslax installs architecture-independent files, including
extension library files, in the /usr/local directories. To specify an
installation prefix other than /usr/local for all installation files,
include the --prefix=prefix option and specify an alternate
location. To install just the extension library files in a different,
user-defined location, include the --with-extensions-dir=dir option
and specify the location where the extension libraries will live::

    % cd build
    % ../configure [OPTION]... [VAR=VALUE]...

If you want to use the regression tests from libxslt, add the
"--with-libxslt-tests=DIR" to the configure command::

    ../configure --with-libxslt-tests=~/work/libxslt-1.1.24

The --enable-debug command turns on SLAX_DEBUG, but debugging
is enabled even without it.  Use the "-d" option to "slaxproc"
to get debug output.

libslax configure Options
~~~~~~~~~~~~~~~~~~~~~~~~~

The libslax-specific options are::

  --enable-warnings    Turn on compiler warnings
  --enable-debug    Turn on debugging
  --enable-readline    Enable support for GNU readline
  --enable-libedit    Enable support for libedit (BSD readline)
  --enable-printflike    Enable use of GCC __printflike attribute

  --with-libxslt-tests=DIR    Include the libxslt tests
  --with-libxml-prefix=PFX           Specify location of libxml config
  --with-libxml-include-prefix=PFX   Specify location of libxml headers
  --with-libxml-libs-prefix=PFX      Specify location of libxml libs
  --with-libxml-src=DIR              For libxml thats not installed yet (sets all three above)
  --with-libxslt-prefix=PFX           Specify location of libxslt config
  --with-libxslt-include-prefix=PFX   Specify location of libxslt headers
  --with-libxslt-libs-prefix=PFX      Specify location of libxslt libs
  --with-libxslt-src=DIR              For libxslt thats not installed yet (sets all three above)
  --with-libcurl-prefix=PFX           Specify location of libcurl config
  --with-extensions-dir=DIR           Specify location of extension libraries

If these packages (libxml2, libxslt, curl) are installed normally,
then their options are not needed.

Running the `make` command
++++++++++++++++++++++++++

Once the `configure` script is run, build the images using the `make`
command:

    % make

Running the Regression Tests
++++++++++++++++++++++++++++

libslax includes a set of regression tests that can be run to ensure
the software is working properly.  These test are optional, but will
help determine if there are any issues running libslax on your
machine.  To run the regression tests::

    % make test

Installing libslax
++++++++++++++++++

Once the software is built, you'll need to install libslax using the
"make install" command.  If you are the root user, or the owner of the
installation directory, simply issue the command::

    % make install

If you are not the "root" user and are using the `sudo` package, use::

    % sudo make install

Verify the installation by viewing the output of "slaxproc --version"::

    % slaxproc --version
    libslax version 0.16.6
    Using libxml 20900, libxslt 10128 and libexslt 817
    slaxproc was compiled against libxml 20900, libxslt 10128 and libexslt 817
    libxslt 10128 was compiled against libxml 20900
    libexslt 817 was compiled against libxml 20900

Special note: xmlcatalog
------------------------

libxml2, libxslt, and libslax do not ship with the DTDs for XHTML,
which can be a massive concern for performance, given that 130 million
requests are made per day to www.w3.org to retrieve them.  This is
annoying and fairly silly.

The fix is unfortunately not always simple.

- For MacOSX, you need to install the "xhtml1" package from MacPorts,
  and then build you own catalog.

    % port install xhtml1

- For FreeBSD, the "xhtml-11" package is in the ports collection, and
  can be installed using the following commands:

    % cd /usr/ports/textproc/xhtml-11
    % sudo make installp

When you are done, your /etc/xml/catalog file should look like this
(see doc/catalog.xml):

    <?xml version="1.0"?>
    <!DOCTYPE catalog PUBLIC "-//OASIS//DTD Entity Resolution XML Catalog V1.0//EN" "http://w
    ww.oasis-open.org/committees/entity/release/1.0/catalog.dtd">
    <catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog">

        <public publicId="-//W3C//DTD XHTML 1.0 Strict//EN"
                uri="file:///opt/local/share/xml/html/4/xhtml1-strict.dtd"/>

        <public publicId="-//W3C//DTD XHTML 1.0 Transitional//EN"
                uri="file:///opt/local/share/xml/html/4/xhtml1-transitional.dtd"/>

        <system systemId="http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"
                uri="file:///opt/local/share/xml/html/4/xhtml1-strict.dtd"/>

        <system systemId="http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
                uri="file:///opt/local/share/xml/html/4/xhtml1-transitional.dtd"/>

        <delegateURI uriStartString="http://www.w3.org/TR/xhtml1/DTD"
                catalog="file://opt/local/share/xml/html/4"/>

    </catalog>

Additional Information
----------------------

If you have trouble installing libslax or have an issue that needs
addressed, please use the `libslax mailing list`_ or open an issue on
the `libslax github`_ page.

.. _libslax mailing list: mailto:libslax@googlegroups.com
.. _libslax github: https://github.com/Juniper/libslax/issues

Additional Notes of Building for Linux
--------------------------------------

Here are my notes from building RPMs under Fedora18.  This is run
under vagrant (vagrantup.com).

Install 'yum'
+++++++++++++

::

    fetch http://yum.baseurl.org/download/3.4/yum-3.4.3.tar.gz
    wget http://yum.baseurl.org/download/3.4/yum-3.4.3.tar.gz
    tar zxf yum-3.4.3.tar.gz 
    cd yum-3.4.3
    make
    make install

Install software prerequisites:
+++++++++++++++++++++++++++++++

::

    yum install rpm-build.x86_64
    yum install redhat-rpm-config.noarch
    yum install libxml2-devel.x86_64
    yum install libxslt-devel.x86_64
    yum install libcurl-devel.x86_64
    yum install libssh2-devel.x86_64
    yum install bison.x86_64
    yum install bison-devel.x86_64
    yum install libedit-devel.x86_64

Optional::

    yum install ntpdate.x86_64
    rpm -i /vagrant/ntpdate-4.2.6p5-8.fc18.x86_64.rpm 

### Install libslax:

::

    wget https://github.com/Juniper/libslax/releases/download/0.16.16/libslax-0.16.16.tar.gz
    tar zxf libslax-0.16.16.tar.gz 
    cd libslax-0.16.16
    sh bin/setup.sh 
    ../configure

### Build an RPM:

Spec files are built automatically for each release::

    cp /vagrant/libslax-0.16.16.tar.gz /root/rpmbuild/SOURCES/
    rpmbuild -ba packaging/libslax.spec 


When using vagrant, first copy the files

    copy tarballs and spec files to /vagrant/
    cp packaging/${package}.spec /vagrant/
    cp ${package}-${version}.tar.gz /vagrant/

    cp /vagrant/tarball.tar.gz /root/rpmbuild/SOURCES/
    rpmbuild -vvv -ba ${package}.spec

    cp /root/rpmbuild/RPMS/x86_64/${package}-${version}-1.fc18.x86_64.rpm /vagrant/

    
