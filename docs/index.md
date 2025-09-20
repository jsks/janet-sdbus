---
title: janet-sdbus
---

`janet-sdbus` is a D-Bus client library for [Janet](https://janet-lang.org) that wraps [sd-bus(3)](https://man.archlinux.org/man/sd-bus.3.en) from systemd. It offers an asynchronous high-level API that is fully integrated with the Janet event loop.

For general information about D-Bus, see the [official specification](https://dbus.freedesktop.org/doc/dbus-specification.html).

Similar libraries in other programming languages:

- [python-sdbus](https://github.com/python-sdbus/python-sdbus)
- [sdbus-cpp](https://github.com/Kistler-Group/sdbus-cpp)

## Limitations

This library is **Linux-only** and not thread-safe. Do not share bus connections across system threads.

Due to the nature of Janet's event loop, any open bus connection **must** be closed prior to exit, otherwise your program will hang indefinitely. This issue is avoided with the use of a context manager.

## Getting Started

`janet-sdbus` requires Janet `>=1.39.0`, libsystemd, and pkg-config.

To install, use `jpm`.

```
$ jpm install https://github.com/jsks/janet-sdbus
```

From within Janet you should now be able to connect to a message bus.

```Janet
(import sdbus)

# Print the known service names on the user bus
(with [bus (sdbus/open-user-bus)]
  (print (sdbus/list-names bus)))
```

Bus connections can either be explicitly closed with `sdbus/close-bus` or the `:close` object method.

Longer, standalone code examples can be found in the [examples directory](https://github.com/jsks/janet-sdbus/tree/main/examples). Each public function in `janet-sdbus` also carries a docstring that can be accessed in Janet with the `doc` helper.

## Type Conversion

Reading and writing D-Bus messages involves translating between native D-Bus wire types and Janet data structures. `janet-sdbus` performs the conversion automatically whenever you append arguments or read replies according to the following conversion scheme.


| D-Bus Type | Name                 | Accepts                  | Returns     |
|:-----------|:---------------------|:-------------------------|:------------|
| y          | Byte                 | number                   | number      |
| b          | Boolean              | boolean                  | boolean     |
| n          | Int16                | number                   | number      |
| q          | UInt16               | number                   | number      |
| i          | Int32                | number                   | number      |
| u          | UInt32               | number                   | number      |
| x          | Int64                | number or core/s64       | core/s64    |
| t          | UInt64               | number or core/u64       | core/u64    |
| d          | Double               | number                   | number      |
| s          | String               | string                   | string      |
| o          | Object Path          | string                   | string      |
| g          | Signature            | string                   | string      |
| h          | Unix File Descriptor | core/file or core/stream | core/stream |
| a          | Array                | array or tuple           | array       |
| v          | Variant              | tuple                    | tuple       |
| ()         | Struct               | array or tuple           | tuple       |
| a{}        | Dictionary           | table or struct          | table       |

Table: The "Accepts" column describes the expected Janet type when appending data, while the "Returns" column is the return type from reading a D-Bus message.

### Integer types

Janet numbers are represented as IEEE-754 doubles with exact integer representation up to 32 bits. Conversion to smaller sized D-Bus integer types will trigger an error if the Janet number is out of range.

For signed and unsigned 64-bit integers, you may pass a Janet number if it can be exactly represented, otherwise use a 64-bit boxed integer type, `core/s64` or `core/u64`.

### Variants

Variants are represented in Janet as two-element tuples, `[signature value]`. The signature must be a valid D-Bus signature string, and the value is validated as if it were appended directly under that signature.

## Calling Methods

`sdbus/call-method` sends a method call asynchronously, suspending the current fiber without blocking the event loop until a reply arrives.

If the D-Bus method expects parameters, you must provide the signature string first followed by the matching Janet values. Type conversion occurs according to the previous [section](#type-conversion).

```Janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  # Methods without parameters may omit the signature
  (def names (sdbus/call-method bus
                 "org.freedesktop.DBus"  # Service name
                 "/org/freedesktop/DBus" # Object path
                 "org.freedesktop.DBus"  # Interface
                 "ListNames"))           # Method
  (print names)

  # A signature must be provided when passing method arguments
  (def units (sdbus/call-method bus
                 "org.freedesktop.systemd1"           # Service name
                 "/org/freedesktop/systemd1"          # Object path
                 "org.freedesktop.systemd1.Manager"   # Interface
                 "ListUnitsByPatterns"                # Method
                 "asas"                               # Signature
                 [] ["dbus.service" "sshd.service"])) # Arguments

    (print units))
```

Users who wish access to a lower-level API may use `sdbus/call-async`. Before calling this function you must create a D-Bus message with `sdbus/message-new-method-call` with method parameters appended using `sdbus/message-append`. This message is passed to `sdbus/call-async` together with a bus connection, a Janet channel, and an optional timeout.

Unlike `sdbus/call-method`, `sdbus/call-async` will not block the current fiber. Instead,  it returns a bus slot which opaquely references the pending method call. Passing this slot to `sdbus/cancel` will cancel the call. Otherwise, the asynchronous results will be written to the user-provided channel. To marshal the contents of a reply message into Janet use `sdbus/message-read`.

## Accessing Properties

Get and set property values using the `sdbus/get-property` and `sdbus/set-property` functions. The former returns a variant, *i.e.*, a Janet tuple with a D-Bus signature and a value. The latter expects a variant in the same format.

```Janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def [sig value] (sdbus/get-property bus
                       "org.freedesktop.DBus"  # Service name
                       "/org/freedesktop/DBus" # Object path
                       "org.freedesktop.DBus"  # Interface
                       "Interfaces"))          # Property

  (printf "Signature: %s and Value: %p" sig value)

  # Bogus example --- do not run as-is
  (sdbus/set-property bus
                      destination
                      path
                      interface
                      "SomeProperty"
                      ["s" "manual"]))
```

## Signals

Signals may be subscribed to with `sdbus/subscribe-signal`. Events are written to a user-provided channel in the form of a tuple, `[status message]`, where status is one of `:ok`, `:error`, or `:close`.

Unlike methods and properties, signal messages are not automatically unpacked into their corresponding Janet values. If you wish to read the contents of a signal message into Janet, use `sdbus/message-read`.

Calling `sdbus/subscribe-signal` will return a bus slot which opaquely references the current signal subscription. To unsubscribe, pass the slot to the `sdbus/cancel` function.

```Janet
(import sdbus ev)

(with [bus (sdbus/open-system-bus)]
  (def ch (ev/chan))
  (def slot (sdbus/subscribe-signal bus "NameOwnerChanged" ch
                 :sender "org.freedesktop.DBus"))

  # Trigger a NameOwnerChanged event
  (sdbus/request-name bus "org.janet.example")

  (match (ev/take ch)
   [status msg]
     (let [payload (sdbus/message-read msg :all)]
       (printf "Status: %s and Payload: %p" status payload)))

  # Unsubscribe
  (sdbus/cancel slot))
```

As a convenience, `janet-sdbus` also provides the `sdbus/subscribe-properties-changed` function to subscribe to `PropertiesChanged` events for a particular interface.

To emit a signal use `sdbus/emit-signal`.

## Introspection

`sdbus/introspect` queries `org.freedesktop.DBus.Introspectable` and returns a
struct that describes the remote object. The top-level keys are:

- `:destination` – queried service name
- `:path` – object path
- `:interfaces` – table with interface names as keyword keys, each
  interface holds member metadata under `:members`
- `:children` – child nodes advertised beneath the object

The metadata mirrors the information exposed in XML.

```Janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def spec (sdbus/introspect bus
               "org.freedesktop.DBus"    # Service name
               "/org/freedesktop/DBus")) # Object path

  (printf "Interfaces: %p" (keys (spec :interfaces)))

  (def interface (get-in spec [:interfaces :org.freedesktop.DBus.Stats]))
  (printf  "Members: %p" (keys interface))
```

## Proxy Objects

Repeatedly typing the same bus, destination, path, and interface can be tedious. Thus, `janet-sdbus` provides an [object oriented](https://janet-lang.org/docs/object_oriented.html) convenience API in the form of proxies.

A proxy is a native Janet table that represents a remote interface. It exposes the members of that interface via object methods.

Interface methods are exposed directly as variadic object methods in the form `(:<method-name> <proxy-object> & args)`{.janet}. Unlike `sdbus/call-method`, you do not need to specify the D-Bus signature when calling methods with arguments through proxies.

Properties are also callable and take the form of `(:<property-name> <proxy-object> &opt value)`. When `value` is nil, the object method acts as a getter and returns the current value of the property. Otherwise, it sets the property to the new value. Unlike `sdbus/set-property`, the `value` argument does not need to be a variant.

Finally, signals can be subscribed to via `(:signals/subscribe <proxy-object> :<signal-name> &opt channel)`. If the channel argument is nil, the object method returns a newly created channel. To unsubscribe, invoke `(:signals/unsubscribe <proxy-object> :<signal-name>)`.

To create a proxy object pass the result from `sdbus/introspect` into the `sdbus/proxy` function together with the bus connection and target interface.

```Janet
(import sdbus)

(with [bus (sdbus/open-system-bus)]
  (def spec (sdbus/introspect bus
               "org.freedesktop.DBus"    # Service name
               "/org/freedesktop/DBus")) # Object path

  (def dbus (sdbus/proxy bus spec :org.freedesktop.DBus))

  (def names (:ListNames dbus))
  (printf "The bus currently exposes %d names" (length names))

  (printf "Feature flags: %q" (:Features dbus)))
```

## Publishing Interfaces

You can publish D-Bus interfaces to a message bus with `sdbus/export`. This function accepts a bus connection, object path, interface name, and a Janet table consisting of methods, properties, and signals created with the following special constructor functions.

- **`sdbus/method`** wraps a Janet function and turns it into a D-Bus
  method.

  When the D-Bus method is invoked, the Janet function will be called
  with the contents of the request message. It must return normal
  Janet values, which will be automatically appended to the reply
  message according to the output signature.

  During execution the Janet function will have access to the bus
  connection, path, and interface name through the dynamic variables
  `:sdbus/bus`, `:sdbus/path`, and `:sdbus/interface`.

  Any errors raised by the Janet function will be passed as D-Bus
  error messages to the calling client.

- **`sdbus/property`** defines a property with a given starting value.

- **`sdbus/signal`** creates a signal. Emit it with `sdbus/emit-signal`.

`janet-sdbus` will immediately begin asynchronously dispatching requests to the newly published interface as soon as `sdbus/export` is called. The call itself to `sdbus/export` will return a bus slot which opaquely references the published interface. The slot may be passed to `sdbus/cancel` to remove the interface.

To aid clients in calling a published interface by using a well-known destination, you may request a service name via `sdbus/request-name`.

```Janet
(import sdbus)

(def env
  {:Echo    (sdbus/method "s" "s" (fn [message] message))
   :Add     (sdbus/method "ii" "i" (fn [x y] (+ x y)))
   :Version (sdbus/property "s" "1.0.0" :w)
   :Tick    (sdbus/signal "s")})

(with [bus (sdbus/open-user-bus)]
  (unless (nil? (sdbus/request-name bus "org.janet.Example" :n))
    (error "Unable to acquire service name"))

  (def slot (sdbus/export bus
                "/org/janet/example"  # Object path
                "org.janet.Example"   # Interface name
                env))                 # Member table

  # Normally we'd block by leaving the bus connection open, letting
  # janet-sdbus continue dispatching client requests. For the sake
  # of example, immediately tear down the interface.
  (sdbus/cancel slot))
```
