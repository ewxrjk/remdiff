# Remote Diff

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

## Future

* Only a few `diff` options are implemented; add more.
* Recursive diff!
* Remote error behavior isn't great.
* Put filenames back into `diff...` lines in output.
