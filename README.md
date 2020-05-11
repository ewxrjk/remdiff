# Remote Diff

`remdiff` is a wrapper for `diff` that access remote files via SFTP.

## Example

    $ remdiff /etc/motd sfere:/etc/motd
    --- /etc/motd   2008-12-29 17:52:31.000000000 +0000
    +++ /dev/fd/3   2020-05-09 17:37:29.269346209 +0100
    @@ -1,7 +1,11 @@
    
    -The programs included with the Debian GNU/Linux system are free software;
    -the exact distribution terms for each program are described in the
    -individual files in /usr/share/doc/*/copyright.
    +Most of the programs included with the Debian GNU/Linux system are
    +freely redistributable; the exact distribution terms for each program
    +are described in the individual files in /usr/doc/*/copyright
    
    Debian GNU/Linux comes with ABSOLUTELY NO WARRANTY, to the extent
    permitted by applicable law.
    +
    +------------------------------------------------------------------------
    +Note that /space on this machine is NOT BACKED UP.
    +------------------------------------------------------------------------

## Build

    autoreconf -si
    ./configure
    make
    sudo make install

## Documentation

    remdiff --help
    man remdiff

## Run

    remdiff LOCAL-PATH HOST:REMOTE-PATH

Either file can be local or remote
(so yes, you can diff two remote files).

## Future

* A few `diff` options are still missing.
* Recursive diff!
