Oregon Scientific WMR100 client for Linux/BSD/etc.
==================================================

Supports
--------

Oregon Scientific WMR100
Oregon Scientific WMR200
Oregon Scientific RMS300A

Introduction
------------

The WMR100/200 use a proprietary protocol for their USB
connection. It's very useful for enthusiasts running a headless Linux
box to collect and analyze data from this link, but unfortunately the
protocol isn't openly documented, nor are clients provided for this
platform.

This simple C program handles the USB protocol of the WMR100, and
translates it an ASCII line-format, easy for parsing/analysing.

You'll need to setup the udev rules (see udev/README) if you want to
run this not as root. This is due to how libhid accesses the USB
ports.

Requisites
----------

libhid-dev (or similarly named) package installed.
pkg-config package installed.

Building
--------

Run 'make'.

To install, copy wmr100 to your path.

Usage
-----

I'd suggest you run ./wmr100, which will dump data to data.log as well
as stdout. You can then process periodically data.log with a script in
python/perl/ruby/your language of choice, and frob with the data that
way.

Alternativally you could adapt the original C code to write to a
database directly instead, but that's more pain that I'm willing to
endure. :-)
