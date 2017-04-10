# PBFLookup
A tool to identify a likely position in Sweden described in texts in Swedish.
## Building the Software

This section describes the steps to generate from source an executable binary of the software.
Some technical background knowledge on software development on Linux is recommended.

### Requirements
The software is written in **C++**, so a compiler toolchain to compile C++ code is required. The software has been tested and developed on *Linux* using GNU GCC's *C++ compiler* (any recent version should suffice).
No tests have been made if the software can be compiled with other compilers or executed on other operating systems like Mac OS X or Windows.

The software makes use of a number of third-party libraries, including:

* [Protocol Buffers](https://developers.google.com/protocol-buffers/) -- Google's data interchange format. Most Linux distributions offer packages for this library under the name `protobuf`. Any recent version should be sufficient.
* [zlib](http://www.zlib.net/) -- a standard (de)compression library. Often already installed as numerous programs make use of this library. Any recent version should be sufficient.
* [Boost](http://www.boost.org/) -- a versatile collection of C++ libraries. Like zlib, this library is often installed due to its widespread use. Any recent version should be sufficient.
* [libconfig](http://www.hyperrealm.com/libconfig/) -- a library for 	processing of structured configuration files. Most distributions (except for Debian stable) provide packages for version 1.5 or later.
* [OSMPBF](https://github.com/scrosby/OSM-binary) -- a Java/C library to read and write OpenStreetMap PBF files. Unfortunately, there is no stable, official release which contains the changes/features required by PBFLookup. Please use the master branch at [Thomas Fischer's fork](https://github.com/thomasfischer-his/OSM-binary) for the time being. At the time of writing, distributions do not ship packages for this library. See the section 'Installing OSMPBF' at the end of this document.

For those C/C++ libraries, headers must be installed. For some Linux distributions, this may require to install each package's accompanying `-dev` (development) package.

In addition to above libraries, to configure the software before the actual compilation, [CMake](https://cmake.org/) and GNU Make are required. Linux distribution provide packages for that.

For example, on a recent Ubuntu system, at least the following packages have to be installed to _compile_ the software: `cmake`, `g++`, `libboost-signals-dev`, `libboost-thread-dev`, `libboost-iostreams-dev`, `libconfig++-dev`, `libprotobuf-dev`, `pkgconf`, and `protobuf-compiler`.

### Configuration

Once the required libraries and tools are installed, the software can be configured. Here, configuration means that it is check if and where the required libraries are installed. To configure the software, simply run (no root permissions required):

```bash
cmake -DCMAKE_INSTALL_PREFIX:PATH=/path/where/to/install/into
```

The path where to install into is usually `/usr/local` for manual installations like this.

Alternatively, an out-of-source build is possible. Please refer to the [CMake documentation](https://cmake.org/documentation/) for details.

Once `cmake` was executed successfully, a Makefile file will have been generated for use in the actual compilation.

### Compilation

To compile the software, simply execute the following command in the same directory where `cmake` was run:

```bash
make -j4
```

The switch `-j4` tells `make` to parallelize the compilation to 4 CPU cores. In case of doubt, this parameter can be omitted.
The resulting executable binary file will be called `pbflookup`.

### Installation

To install the software into the directory structure as specified during the configuration step (see above), simply issue:

```bash
sudo make install
```

The `sudo` command is only necessary if the installation prefix points to a directory not writable by the current user.

Later and as long as the directory where the software was built in still exists, the software can be *uninstalled* by issuing:

```bash
sudo make uninstall
```

### Preparing to Use the Software

Before the compiled software can be used, a number of preparatory steps have to be performed as detailed in the following subsections

* Acquisition of map data for offline use,
* editing of configuration file(s), and
* editing of testsets (example test cases to be checked by the software)

#### Downloading Map Data

A requirement to run the software is a current snapshot of the OpenStreetMap data for Sweden. Such a snapshot is available at [Geofabrik](http://download.geofabrik.de/europe/sweden.html), a company providing services based on OpenStreetMap data.

A snapshot of the map data on Sweden is available in different formats.
Required for this software is the format `.osm.pbf`, available at [http://download.geofabrik.de/europe/sweden-latest.osm.pbf](http://download.geofabrik.de/europe/sweden-latest.osm.pbf) at the time of writing. As the map of Sweden gets updated regularly by [active contributors](http://www.openstreetmap.org/), it is recommended to download an updated snapshot about once per month.

#### Editing the Configuration File

PBFLookup ships with a default configuration file: `sweden.config`. For the software find this configuration file, it must be either placed in the current working directory, or the path and filename must be provided as a command line argument like this:

```bash
pbflookup /path/to/sweden.config
```

If only a map name like `sweden` or a relative path is given, the software will start looking for a matching configuration file starting in the current working directory.

The configuration file's format follows the format as described by in the [libconfig's manual](http://www.hyperrealm.com/libconfig/libconfig_manual.html):
Empty lines and lines starting with a hash symbol (`#`) will be ignored. Other configuration files can be included using the `@include` statement. The actual configuration entries are key-value pairs, separated by an equal sign, i. e. `key=value`.

All relative filenames used in the configuration file will be resolved relative to the configuration file's path, not the current working directory or the executable binary's path.
For absolute paths, `~` is allowed as placeholder for the user's home directory.

A number of configuration options (key-value pairs) are supported to control the software.
For some options, reasonable default values will be assumed if no value is set for this option in the configuration file (details in the list below).

The most common configuration options include:

* `mapname` should be set to `sweden` (default), no need to be changed.
* `osmpbffilename` is the relative (to the configuration file) or absolute filename of the map file.
The default for this value is the `mapname` followed by `-latest.osm.pbf`, i. e. exactly the name as the OpenStreetMap data downloaded from GeoFabrik.
* `osmpbffilenames` is similar to `osmpbffilename', but accepts a comma-separated list of filenames.
* `tempdir` where temporary files are stored. By default `/tmp` is used.
* `pidfile` where a text file containing the current process's PID will be written to, which is necessary for some cases where PBFLookup is started as a system service.
By default, the PID file is placed inside the directory specified in the environment variable `XDG_RUNTIME_DIR`; the temporary directory is used as a fall-back.
* `logfile` where log messages (in most cases the same as shown during program execution) are written to. By default, a file in the temporary directory is used, containing the map name and the current timestamp in its filename.
* `stopwordfilename` should point to the provided file `stopwords-sweden.txt` (default value) which contains more than 400 words (one word per line, UTF-8-encoded) from the Swedish language that should get skipped when processing text as those words are most likely not referring to a geographic location. Examples include *efter* or *vilken*.

The software may either run either an interactive web server until stopped manually or non-interactively process given input texts (called ‘testsets’, see later sections for details on used testsets) and exit once all texts have been processed. If testsets are configured, they can be used for benchmarking and testing the software in the non-interactive mode. In the interactive web server mode, the testsets will be provided as examples to the user

To enable the software internal web server, the following configuration options should be set. Disabling or removing those options will make the software run in non-interactive mode.

* `http_port` to specify on which port the server should listen. Only non-privileged port numbers are accepted.
* `http_interface` on which network interface(s) the server should listen. Valid options include:
  * `local` listen on the loopback device only. The server will only be accessible from the local machine via IP address `127.0.0.1`.
  * `any` listen on any interface making the server accessible from any other machine.
  * _A network interface's IP address_. The server will only listen on this interface for incoming packets.
* `http_public_files` describes the directory from where 'normal' files are delivered from by the webserver, for example `.css` files for `favicon.ico`.

An example configuration file where the web server is configure to listen on port 5274 for local connections only may look like this:

```
mapname = "sweden"
logfile = "${tempdir}/${mapname}-${timestamp}.log"
http_port = 5274
http_interface = "local"
http_public_files = "public"
```

#### Testset Configuration File

Testsets can be configured in the main configuration file described above, although it is recommended for organisatorial purposes to place testsets in one or several additional configuration files that can be included into the main configuration file as exemplified here:

```
@include "testsets-sweden.config"
```

A testset configuration file has a more complex structure than the main configuration file, mainly to accommodate complex data structures like lists or maps. The structure still follows the design as outlined in _libconfig_'s manual.


The single main key for the testset configuration file is `testset`, the value is a list inside round parentheses (`(`...`)`), where list items are separated by commas (`,`). Each list item is a map, enclosed in curly brackets (`{`...`}`). The map is described in a linebreak-separated list of key-value pairs just like the main configuration file.

A map for a testset may contain the following key-value pairs:

* `name` is a short, human-readable description for just this testset.
* `latitude` and `longitude` are the expected result position for this testset. Both values have to be expressed with decimal fractions, i. e. `23.4583` instead of `23° 27' 30"`. If a test has multiple coordinates like the 'Kulturskolor' example above, coordinates have to be written as an array, i. e. in square brackets, values are separated by commas.
* `text` is the text in Swedish to be analyzed. Regular Swedish text including common punctuation may be provided, but certain character may be problematic. For example, as the text is terminated by a double quotation character (`"`), such a character may occur in the text.
* `svgoutputfilename` is the only optional parameter, providing a filename where a SVG file with a map of Sweden and the result of the analysis shall be written into. If the filename ends with `.gz`, the SVG file will get gzip-compressed.

A complete example for a configuration file which can be imported into te main configuration file may look like this:

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

## Starting PBFLookup

During its execution, PBFLookup has a peak memory (RAM) consumption of up to 5GB. Therefore, 8GB is recommended as the minimum physical memory size available on the machine this software is running on.

### Starting PBFLookup via systemd

It is possible to start PBFLookup via systemd, i. e. as a daemon running in background. It is indeed the recommended way if the software is to be run continuously and meant to provide its services to the public.
PBFLookup can be run as either a system or user service. In any case, it should not be run as user root (UID 0).

A systemd service file for PBFLookup named `/etc/systemd/system/pbflookup.service` may look like this:

```
[Unit]
Description=A service to georeference Swedish text
After=network.target

[Service]
Type=simple
WorkingDirectory=/etc/pbflookup
ExecStart=/usr/local/bin/pbflookup
KillMode=mixed
MemoryLimit=6G
TasksMax=8
User=pbflookup
Group=pbflookup
RuntimeDirectory=pbflookup

[Install]
WantedBy=multi-user.target
```

This file makes the following assumptions:

* The program's binary is located at `/usr/local/bin/pbflookup`.
* The configuration file `sweden.config` is located in directory `/etc/pbflookup`.
* There exists a user 'pbflookup' and group 'pbflookup' which will be used to run the software.

Additional configuration steps that may be necessary for a functional setup include:

1. Copy the stopwords file, testsets files, and the built-in web server's `public` directory to `/etc/pbflookup`.
1. Download the latest `.osm.pbf` file into `/var/cache`
1. Adopt the configuration file `/etc/pbflookup/sweden.config` asexemplified in below to reflect the placement of files.

```
mapname = "sweden"
logfile = "${tempdir}/${mapname}-${timestamp}.log"
osmpbffilename = "/var/cache/${mapname}-latest.osm.pbf"
stopwordfilename = "stopwords-${mapname}.txt"
pidfile = "/run/pbflookup/pbflookup.pid"
http_port = 5274
http_interface = "any"
http_public_files = "public"

@include "testsets-sweden-polisen.config"
@include "testsets-sweden-easy.config"
@include "testsets-sweden-hard.config"
```

Due to the considerable start-up time, the so-called 'socket activation' is not recommended.

### Files Written in Temporary Directory
During execution, temporary files of about 1GB size will be written to the temporary directory. Those files allow to improve the start-up time for subsequent runs. It is safe to remove those files between runs, it will only increase the next start-up time.

Log messages printed during execution will be written to a log file. Unless configured differently, this file will be placed in the temporary directory as well.

## Web Server

The web server will be automatically started if a valid `http_port` value is specified. Only non-privileged port numbers will be accepted, i. e. the valid range is 1024 to 65535. The web server will listen on the interfaces as specified by `http_interface`.
The server supports only plain, unencrypted HTTP, no SSL or TLS.

To test the web server, simply visit the server's main page using a browser.
If the interface is `local` or `any` and the software is running on the local machine, the default address is `http://127.0.0.1:5274`.

### Web Interface

The web interface is meant as a human interface to test the search engine. For machine-to-machine communication, e. g. if the software is to be used by a mobile phone app, please see below for details.

The search form for the user to enter search requests is delivered by visiting the root location, e. g. `http://127.0.0.1:5274`. Here, either pre-configured tests can be selected or a free text can be entered. The result can be returned as HTML (a web page), JSON data, or XML data.

Upon posting a search request, the software will perform a search on its geodata and return a table of results, where the results most likely to be the best ones come first. Each table row contains the most likely location, a excerpt of a map showing the location, and a short description how this results was found, e. g. which OpenStreetMap nodes, ways, or relations were contributing. Links to the OpenStreetMap webpage are provided where appropriate.

An instance of this web interface is running on a server of the University of Skövde, accessible via: http://fish.research.nsa.his.se:9876/

### Machine-to-Machine Interface

To make the software usable for third-party software, such as mobile phone apps, search requests can be posted without going through the web interface and results can be requested to be returned in machined-readable formats like JSON or XML.

To send a search request, a HTTP POST message must be sent to the web server's root location (`/`). The only parameter is `text`, i. e. the query text. The request result format (JSON or XML) can be specified either as a HTTP header (`Accept:`...) or as a query parameter to the URL (...`?accept=`...).
The following example will query the web server to perform a localization request on the input `Högskolan i Skövde` and return the result as JSON data.

```
curl -X POST 'http://127.0.0.1:5274/' -H "Accept: application/json" -d "text=Högskolan i Skövde"
```

The request as sent in above example will look like this:

```
POST / HTTP/1.1
Host: 127.0.0.1:5274
Accept: application/json

text=Högskolan i Skövde
```

To make the same search, but requesting XML data via the Accept parameter in the URL, the request would look like this:

```
POST /?accept=text/xml HTTP/1.1
Host: 127.0.0.1:5274

text=Högskolan i Skövde
```

Note that the text data is expected to be encoded as UTF-8. In above example, the letter 'ö' is represented by two bytes (0xc3 and 0xb6).

The returned data contains the same information for both JSON and XML, but is formatted and structured differently. The abbreviated results for above requests look like shown in the following examples. For the JSON request, the answer would be:

```
{
  "cputime[ms]": 6.845,
  "results": [
    {
      "latitude": 58.3967,
      "longitude": 13.8546,
      "scbareacode": 1496,
      "municipality": "Skövde",
      "county": "Västra Götalands Län",
      "url": "https://www.openstreetmap.org/?mlat=58.39672&mlon=13.85455#map=13/58.39672/13.85455",
      "image": "https://a.tile.openstreetmap.org/13/4411/2450.png",
      "origin": {
        "description": "Places inside admin bound: skövde > högskolan",
        "elements": [
          "node/759311925"
        ]
      }
    },
    {
      "latitude": 58.3887,
      "longitude": 13.8460,
      "scbareacode": 1496,
      "municipality": "Skövde",
      "county": "Västra Götalands Län",
      "url": "https://www.openstreetmap.org/?mlat=58.38870&mlon=13.84604#map=13/58.38870/13.84604",
      "image": "https://c.tile.openstreetmap.org/13/4411/2450.png",
      "origin": {
        "description": "Local/global places: Node 25508588 (Skövde) > Node 2612842706 (Skövde natur)",
        "elements": [
          "node/25508588",
          "node/2612842706"
        ]
      }
    }
  ]
}
```

The corresponding XML result looks like this:

```
<?xml version="1.0" encoding="UTF-8" standalone="yes" ?>
<pbflookup>
  <cputime unit="ms">6.845</cputime>
  <results>
    <result>
      <latitude format="decimal">58.3967</latitude>
      <longitude format="decimal">13.8546</longitude>
      <scbareacode>1496</scbareacode>
      <municipality>Skövde</municipality>
      <county>Västra Götalands Län</county>
      <url rel="openstreetmap">https://www.openstreetmap.org/?mlat=58.39672&amp;mlon=13.85455#map=13/58.39672/13.85455</url>
      <image rel="tile">https://a.tile.openstreetmap.org/13/4411/2450.png</image>
      <origin>
        <description>Places inside admin bound: skövde &gt; högskolan</description>
        <elements>
          <node>759311925</node>
        </elements>
      </origin>
    </result>
    <result>
      <latitude format="decimal">58.3887</latitude>
      <longitude format="decimal">13.8460</longitude>
      <scbareacode>1496</scbareacode>
      <municipality>Skövde</municipality>
      <county>Västra Götalands Län</county>
      <url rel="openstreetmap">https://www.openstreetmap.org/?mlat=58.38870&amp;mlon=13.84604#map=13/58.38870/13.84604</url>
      <image rel="tile">https://c.tile.openstreetmap.org/13/4411/2450.png</image>
      <origin>
        <description>Local/global places: Node 25508588 (Skövde) &gt; Node 2612842706 (Skövde natur)</description>
        <elements>
          <node>25508588</node>
          <node>2612842706</node>
        </elements>
      </origin>
    </result>
  </results>
</pbflookup>
```

The result representation is straight forward: All results are collected in a `results` list/array. Each individual result consists of a latitude/longitude in decimal representation, the municipality where the result is located in (both as name and SCB code), county, links to OpenStreetMap's map (website and map tile URL), and a description how the result was determined, e. g. which nodes, ways, or relations contributed.

The results will always be delivered with a transfer encoding of 8 bits using charset UTF-8.

Please note that the map tile images are not 'for free'. They get downloaded from OpenStreetMap's own servers, which are financed through donations. Any larger or commercial usage must be approved by the OpenStreetMap system administrators or a private tile server must be used.

## Installing OSMPBF

To install OSMPBF (also known as `OSM-binary') from its Git sources, perform the following steps:

1. Create a subdirectory where the sources of this project can be placed in, e. g. `git`
1. In a terminal which has this source subdirectory as its current working directory, run the following command:
```
git clone https://github.com/thomasfischer-his/OSM-binary.git
```
1. Create a temporary build directory, e. g. `/tmp/build-OSM-binary`.
1. Inside build directory, run:
```
cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr/local ~/git/OSM-binary && make -j4 && sudo make -j4 install
```
