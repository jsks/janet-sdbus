Janet bindings to [sd-bus](https://www.freedesktop.org/software/systemd/man/latest/sd-bus.html).

```janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def names (sdbus/call-method bus "org.freedesktop.DBus"
                                    "/org/freedesktop/DBus"
                                    "org.freedesktop.DBus"
                                    "GetId"))
  (pp names))

```

### Installation

Note, this project is currently in an alpha stage and can only be built against the latest master branch of [Janet](https://github.com/janet-lang/janet).

```
$ jpm install https://github.com/jsks/janet-sdbus
```

### Development

```sh
$ jpm clean && jpm --build-type=debug build
$ jpm -l install
$ LD_PRELOAD=/usr/lib/libasan.so jpm -l test
```
