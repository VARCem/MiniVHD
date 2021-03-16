## Building the MiniVHD library (and tools) from Source


#### Fred N. van Kempen, <waltje@varcem.com>

#### Last Updated: 3/16/2021


These instructions guide you in building the **MiniVHD** library from
its published sources. Currently, it can be built for **Windows** and
**Linux** (Debian-tested) platforms.  More supported platforms will be
added as time permits.


#### Microsoft Windows

#### Get the sources.

For the Windows environment, two methods of compiling these sources are currently possible:

##### Using Microsoft's Visual Studio development system.
Tested with versions **2015** ("VC14") and **2017*** ("VC15"), this is now a
fairly easy process.

1.  Install the Visual Studio system (the *Community* release is sufficient) and make sure it works.

2.  Go to the *win32\* folder, then to the *vc14* folder for Visual Studio 2015, or to the *vc15* folder for the 2017 release of Visual Studio.

3.  Double-click on the *MiniVHD.sln* solution file, and Visual Studio will load the project for you.

4.  Select your preferred type of build (x86 or x64, Debug or Release) and build the project.

**All done!**  You can repeat this for other build types if so desired.

For those who prefer to use **command-line tools** to process a *Makefile* (the author is such an oddity), you can also open up a **Visual Studio Command Prompt** from the Start Menu, and type the command:

  `D:`

  `cd \minivhd\src`

  `make -f win32\Makefile.VC`
  
and voila, there it goes. If things are set up properly, this is about the fastest way to compile the sources on Windows.

##### Using the MSYS2 "MinGW" toolset.
Building the emulator is also possible using the free **MinGW** compiler toolset, which is based on the well-known *gcc* toolset. Although also available as a standalone project, we will use the version that is part of the **MSYS2** project, which aims to create a *usable Linux-like environment on the Windows platform*. The MinGW toolset is part of that environment.

1.  Download the most recent **MSYS2** distribution for your system.

    Please note that MSYS2 is a *64-bit application*, even though it
    (also) contains a 32-bit MinGW compiler.

2.  Install MSYS2 onto your system, using either *C:\msysS2* or the
    suggested *C:\msysS64* folders as a root folder.

3.  The standard installation of MSYS2 does not include a compiler
    or other 'build tools'. This is surprising, given the main
    objecttive of the project, but so be it.

    The easiest way to install packages like these is to use its
    built-in package manager, **pacman**.  You can install a full
    "base setup" for GCC and friends with the command:

      `pacman -S --needed base-devel mingw-w64-i686-toolchain \`

      `mingw-w64-x86_64-toolchain git subversion mercurial \`

      `mingw-w64-i686-cmake mingw-w64-x86_64-cmake \`

      `mingw-w64-i686-libpng mingw-w64-x86_64-libpng`

    (the backslashes are only for clarity; you can type it as one
    single line if you wish.)

    Pacman will show you the various packages and sub-packages to
    install, just type **ENTER** to agree, followed by a **y** to begin
    downloading and installing these packages.

4.  Make sure this step worked, by typing commands like:

     `cc`

     `gcc`

     `make`

    just so we know this is done.

5.  You may want to install a text editor of choice; in our case, we
    will install the **vi** (or, really, **Vim**) editor, using the package
    manager:

      `pacman -S vim`

    Again, make sure this step worked.

6.  Finally, we are good to go at compiling the MiniVHD sources.

    You are now ready to build, using a command like:

      `make -f win32/Makefile.MinGW`

    which will hopefully build the application's executables.

With these steps, you have set up a build environment, suitable
to build the MiniVHD project files for Windows.
