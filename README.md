# PBFLookup
A tool to identify a likely position in Sweden described in texts in Swedish.
## Building the Software
### Requirements
The software is written in **C++**, so a compiler toolchain to compile C++ code is required. The software has been tested and developed on *Linux* using GNU GCC's *C++ compiler*.

The software makes use of a number of third-party libraries, including:

* [Protocol Buffers](https://developers.google.com/protocol-buffers/) -- Google's data interchange format. Most Linux distributions offer packages for this library under the name `protobuf`. Any recent version should be sufficient.
* [zlib](http://www.zlib.net/) -- a standard (de)compression library. Often already installed as numerous programs make use of this library. Any recent version should be sufficient.
* [Boost](http://www.boost.org/) -- a versatile collection of C++ libraries. Like zlib, this library is often installed due to its widespread use. Any recent version should be sufficient.
* [libconfig](http://www.hyperrealm.com/libconfig/) -- a library for 	processing of structured configuration files. Most distributions provide packages for version 1.5 or later.
* [OSMPBF](https://github.com/scrosby/OSM-binary) -- a Java/C library to read and write OpenStreetMap PBF files. Unfortunately, there is no stable, official release which contains the changes/features required by PBFLookup. Please use the master branch at [Thomas Fischer's fork](https://github.com/thomasfischer-his/OSM-binary) for the time being. At the time of writing, distributions do not ship packages for this library.

For those C/C++ libraries, headers must be installed. For some Linux distributions, this may require to install each package's accompanying `-dev` (development) package.

In addition to above libraries, to configure the software before the actual compilation, [CMake](https://cmake.org/) and GNU Make are required. Linux distribution provde packages for that.

### Configuration

Once the required libraries and tools are installed, the software can be configured. Here, configuration means that it is check if and where the required libraries are installed. To configure the software, simply run (no root permissions required):

```bash
cmake -DCMAKE_INSTALL_PREFIX:PATH=/path/where/to/install/into
```

The path where to install into is usually `/usr/local` for manual installations like this.

Alternatively, an out-of-source build is possible. To achieve that, create directory `/tmp/pbflookup-build`, go to this directory and launch from there

```bash
cmake /path/where/pbflookup/sources/are/ -DCMAKE_INSTALL_PREFIX:PATH=/path/where/to/install/into
```

On Linux, if successful, a `Makefile` will be generated for use in the actual compilation.

### Compilation

To compile the software, simply execute the following command in the same directory where `cmake` was run:

```bash
make -j4
```

The switch `-j4` tells `make` to parallize the compilation to 4 CPU cores. In case of doubt, this parameter can be omitted.

### Installation

To install the software into the directory structure as specified during the configuration step (see above), simply issue:

```bash
make install
```

Later and as long as the directory where the software was built in still exists, the software can be *uninstalled* by issuing:

```bash
make uninstall
```

## Running the Software

During its execution, PBFLookup has a peak memory (RAM) consumption of up to 4GB. Therefore, 8GB is recommended as the minimum physical memory size available on the machine this software is running on.

### Downloading Map Data
A requirement to run the software is a current snapshot of the OpenStreetMap data for Sweden. Such a snapshot is available at [Geofabrik](http://download.geofabrik.de/europe/sweden.html), a company providing services based on OpenStreetMap data.

A snapshot of the map data on Sweden is available in different formats.
Required for this software is the format `.osm.pbf`, available at [http://download.geofabrik.de/europe/sweden-latest.osm.pbf](http://download.geofabrik.de/europe/sweden-latest.osm.pbf) at the time of writing. As the map of Sweden gets updated regularly by [active contributors](http://www.openstreetmap.org/), it is recommended to download an updated snapshot about once per month.

### Editing the Configuration File

PBFLookup ships with a default configuration file: `sweden.config`. For the software find this configuration file, it must be either placed in the current working directory, or the path and filename must be provided as a command line argument like this:

```bash
pbflookup /path/to/sweden.config
```

The configuration file has a simple structure: empty lines and lines starting with a hash symbol (`#`) will be ignored. Other configuration files can be included using the `@include` statement. The actual configuration entries are key-value pairs, separated by an equal sign, i.e. `key=value`. Reasonable default values will be assumed for configuration entries not set in the configuration file.

The most common configuration options include:

* `mapname` should be set to `sweden` (default), no need to be changed.
* `osmpbffilename` is by default the value of `mapname` followed by `-latest.osm.pbf`, i.e. exactly the name as the OpenStreetMap data downloaded from GeoFabrik. Use a relative or absolute path (`~` is allowed as placeholder for the user's home directory) to point to a different location/filename.
* `tempdir` where temporary files are stored. By default `/tmp` is used.
* `logfile` where log messages (in most cases the same as shown during program execution) are written to. By default, a file in the temporary directory is used, containing the map name and the current timestamp in its name.
* `stopwordfilename` should point to the provided file `stopwords-sweden.txt` (default value) which contains more than 400 words (one word per line, UTF-8-encoded) from the Swedish language that should get skipped when processing text as those words are most likely not referring to a geographic location. Examples include *efter* or *vilken*.

Preconfigured testsets can be included through an include statement like this (filename is an example only):

```
@include "testsets-sweden.config"
```

Simply disable or remove those lines to skip running those tests.

To enable the software internal web server, the following configuration options should be set:

* `http_port` to specify on which port the server should listen.
* `http_interface` on which network interface(s) the server should listen. Valid options include:
  * `local` listen on the loopback device only. The server will only be accessible from the local machine via IP address `127.0.0.1`.
  * `any` listen on any interface making the server accessible from any other machine.
  * A network interface's IP address. The server will only listen on this interface for incoming packets.
* `http_public_files` describes the directory from where 'normal' files are delivered from by the webserver, for example `.css` files.

To (temporarily) disable the web server, at least option `http_port` needs to be disabled by prefixing it with a hash symbol (`#`).

### Starting PBFLookup

PBFLookup expects its configuration files and OpenStreetMap data in the current working directory.
If the program was compiled in the smae directory as where those files are located, the program can be launched simply by

```bash
./pbflookup
```

If an out-of-source build was chosen, start the program with

```bash
/path/to/the/build/directory/pbflookup
```

### Files Written in Temporary Directory
During execution, temporary files of about 500MB will be written to the temporary directory. Those files allow to improve the start-up time for subsequent runs. It is safe to remove those files between runs, it will only increase the next start-up time.

PBFLookup can also be configured to write SVG drawings of Sweden including markers about results. If the specified output filename ends with `.gz`, the SVG file will get gzip-compressed (file size up to 3MB), otherwise the file will stay uncompressed (plain SVG, file size about 10MB).

Log messages printed during execution will be written to a log file. Unless configured differently, this file will be placed in the temporary directory as well.

## Testset Configuration File

The testset configuration file is by default included from the main configuration file (`@include` statement).
The testset file has a more complex structure than the main configuration file, mainly to accomodate complex data structures like lists or maps.

The single main key for the testset configuration file is `testset`, the value is a list inside round parentheses (`(`...`)`), where list items are separated by commas (`,`). Each list item is a map, enclosed in curly brackets (`{`...`}`). The map is described in a linebreak-separated list of key-value pairs just like the main configuration file.

```
testsets = (
  {
    name = "Skövde tingsrätt"
    latitude = 58.38662
    longitude = 13.84676
    text = "Skaraborgs tingsrätt förbereder inför rättegången mot den mordmisstänkte 35-åringen. En del i förberedelserna är en begäran om att få en personutredning på den misstänkte.",
    svgoutputfilename = "${tempdir}/testset-sweden-skovde-tingsratt.svg.gz"
  },
  {
    name = "Travbanan Axvall"
    latitude = 58.3884
    longitude = 13.5728
    text = "På travbanan i Axvall vill man samla alla hästutbildningar i regionen.",
    svgoutputfilename = "${tempdir}/testset-sweden-travbanan-axvall.svg.gz"
  },
  {
    name = "Kulturskolor",
    latitude = [57.1652, 56.4591, 56.4121],
    longitude = [16.0263, 13.5928, 16.0033],
    text = "Tre kommuner i Kalmar och Kronobergs län har ingen kulturskola alls. Det är Högsby, Markaryd och Torsås. Övriga kommuner erbjuder lektioner på sina musik- eller kulturskolor, men det är stora skillnader i utbudet av kurser."
  }
)
```

A map for a testset may contain the following key-value pairs:

* `name` is a short, human-readable description for just this test set
* `latitude` and `longitude` are the expected result position for this testset. Both values have to be expressed with decimal fractions, i.e. `23.4583` instead of `23° 27' 30"`. If a test has multiple coordinates like the "Kulturskolor" example above, coordinates have to be written as an array, i.e. in square brackets, values are separated by commas.
* `text` is the text in Swedish to be analyzed. Regular Swedish text including common punctuation may be provided, but certain character may be problematic. For example, as the text is terminated by a double quotation character (`"`), such a character may occur in the text.
* `svgoutputfilename` is the only optional parameter, providing a filename where a SVG file with a map of Sweden and the result of the analysis shall be written into. If the filename ends with `.gz`, the SVG file will get gzip-compressed.

