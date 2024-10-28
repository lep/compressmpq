

# What is it

WarCraft 3 stores files inside the mpq compressed. The algorithm used is deflate.
This tool uses a deflate implementation by [Google](https://en.wikipedia.org/wiki/Zopfli) which takes around 100 times longer but also produces something between
3% and 10% smaller files.
For most of you this probably is pretty useless but i assume that for people who scratch at the 8MB map limit every saved byte
is precious.
So this tool is no wonder tool so first use other mechanisms of lowering your maps size such as model and image compression.

# How to use it

In the best case it's very easy to use as it expects only two files: the input map-file and the output map-file.

	$ compress.exe mymap.w3x mymap_min.w3x

But you probably want to use it in conjunction with other tools such as the w3mapoptimizer.
The w3mapoptimizer removes the listfile which is essential1 for this tool to work.
If no listfile is found or your listfile isn't sufficient it will say so.
So to use it with w3mapoptimizer you can provide an additional listfile:

	$ compress.exe -l /path/to/listfile.txt mymap.w3x mymap_min.w3x

Additional options and tweaks are explained below.

--------

If you want to use this tool you should avoid map protectors.
First of all: they don't work, if WarCraft can read your files so can everybody else.
But secondly either if you use this tool after any protectors it probably wont work as i only parse valid MPQs as written by worldedit.
Or if you use this tool first and then some protector it will probably undo the compression made by this.
So you should use this tool as your last step in publishing your map.

# Options

Option	| Default | Explanation
--------|---------|------------
--cache, -c | not set | Uses a persistent read-write cache.
--threads, -t | 2 | The number of threads that are started. A good value would be the number of cores your CPU has.
--iterations, -i | 15 | How many iterations are spent on compressing every file. Increasing this slows down the tool even further.
--listfile, -l | not set | Additional listfile if the map internal listfile is not sufficient/non existent.
--shift-size, -s | 15 | Sets the mpqs blocksize to `512*2^(shiftsize)`. Blizzard uses 3 and w3mapoptimizer recomends 7.
--block-splitting-max | 15 | Maximum amount of blocks to split into (0 for unlimited, but this can give extreme results that hurt compression on some files).

# License

This tool is released under GPL v3 but it uses parts of
- Apache Portable Runtime which is released under Apache v2.0
- miniz which is released under public domain.
- Stormlib which is released under MIT
- Zopfli which is released under Apache v2.0
