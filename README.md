![ci workflow](https://github.com/jsks/janet-sdbus/actions/workflows/ci.yml/badge.svg)

Janet bindings to [sd-bus](https://www.freedesktop.org/software/systemd/man/latest/sd-bus.html).

```janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def names (sdbus/call-method bus "org.freedesktop.DBus"
                                    "/org/freedesktop/DBus"
                                    "org.freedesktop.DBus"
                                    "ListNames"))
  (pp names))

```

### Installation

This project requires Janet `>=1.39.0`, libsystemd, and pkg-config.

```
$ jpm install https://github.com/jsks/janet-sdbus
```

### Development

```sh
$ jpm clean && jpm --build-type=develop build
$ jpm -l install
$ LD_PRELOAD=/usr/lib/libasan.so jpm -l test
```
