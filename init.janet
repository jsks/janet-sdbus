# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./native :prefix "" :export true)
(import ./introspect :prefix "" :export true)

(defn call-method
  ```
  Send a method call to a D-Bus service. Suspends the current fiber
  without blocking the event loop. Returns the contents of the reply
  message.

  If the method expects arguments, the first rest argument must be a
  D-Bus signature string.
  ```
  [bus destination path interface method & rest]
  (def msg (message-new-method-call bus destination path interface method))
  (def signature (first rest))
  (unless (or (nil? signature) (empty? signature))
    (message-append msg signature ;(slice rest 1)))
  (with [ch (ev/chan)]
    (call-async bus msg ch)
    (match (ev/take ch)
      [:ok msg] (message-read-all msg)
      [:error err] (error err)
      [:close _] (error "D-Bus connection closed")
      result (errorf "Unexpected result: %p" result))))

(defn get-property
  ```
  Get a property from a D-Bus service. Returns a variant in the form
  of a tuple, `[type value]`, where `type` is a D-Bus signature
  string.
  ```
  [bus destination path interface property]
  (call-method bus destination path
               "org.freedesktop.DBus.Properties" "Get"
               "ss" interface property))

(defn set-property
  ```
  Set a property on a D-Bus service.

  `variant` must be a D-Bus variant encoded as a Janet tuple with two
  members: a valid D-Bus signature string and a corresponding value.
  ```
  [bus destination path interface property variant]
  (call-method bus destination path "org.freedesktop.DBus.Properties" "Set"
               "ssv" interface property variant))

(defn emit-signal
  ```
  Emit a D-Bus signal. If the signal expects arguments, the first
  rest argument must be a D-Bus signature string.
  ```
  [bus path interface signal & rest]
  (def msg (message-new-signal bus path interface signal))
  (def signature (first rest))
  (unless (or (nil? signature) (empty? signature))
    (message-append msg signature ;(slice rest 1)))
  (message-send msg))

(defmacro- symbolic-kvs [& args]
  (with-syms [$syms $values]
    ~(let [,$syms ',args
           ,$values [,;args]]
       (keep |(unless (nil? $1) (string/format "%s='%s'" $0 $1)) ,$syms ,$values))))

(defn subscribe-signal
  ```
  Subscribe to a D-Bus signal. Returns a bus slot that may be passed
  to `sdbus/cancel` to unsubscribe.

  This builds a match rule, "type='signal',member='<member>'", with
  optional sender, path, and interface, and forwards it to
  `sdbus/match-async`.

  Signal messages are written to the channel, `chan`, as `[:ok msg]`,
  `[:error msg]`, or `[:close msg]`.
  ```
  [bus member chan &named sender path interface]
  (def rules (-> (symbolic-kvs member sender path interface)
                 (string/join ",")))
  (match-async bus rules chan))

(defn subscribe-properties-changed
  ```
  Subscribe to a PropertiesChanged signal for a given
  interface. Returns a bus slot that may be passed to `sdbus/cancel`
  to unsubscribe.

  PropertiesChanged messages are written to the channel, `chan`, as
  `[:ok msg]`, `[:error msg]`, or `[:close msg]`.
  ```
  [bus interface chan &named sender path]
  (def base ["type='signal'" "member='PropertiesChanged'"
             "interface='org.freedesktop.DBus.Properties'"])
  (def rules (-> (symbolic-kvs sender path)
                 (array/concat base)
                 (string/join ",")))
  (match-async bus rules chan))

(defn introspect
  ```
  Get introspection data for a D-Bus object in the form of a Janet
  struct.
  ```
  [bus destination path]
  (-> (call-method bus destination path "org.freedesktop.DBus.Introspectable" "Introspect")
      (parse-xml :destination destination :path path)))

(defn- proxy-method [name method]
  (def sig (-> (map |(get $ :type) (get method :in)) (string/join)))
  (fn [self & args]
    (let [bus (self :bus)
          destination (self :destination)
          path (self :path)
          interface (self :interface)]
      (call-method bus destination path interface name sig ;args))))

(defn- proxy-property [name property]
  (def sig (get property :type))
  (fn [self &opt value]
    (let [bus (self :bus)
          destination (self :destination)
          path (self :path)
          interface (self :interface)]
      (if (nil? value)
        (get (get-property bus destination path interface name) 1)
        (set-property bus destination path interface name [sig value])))))

(defn- proxy-subscribe [self name &opt ch]
  (default ch (ev/chan))
  (var *slot* nil)
  (let [bus (self :bus)
        interface (self :interface)
        path (self :path)
        name (string name)]
    (if (= name "PropertiesChanged")
      (set *slot* (subscribe-properties-changed bus interface ch :path path))
      (set *slot* (subscribe-signal bus name ch :path path :interface interface))))
  (set ((self :subscriptions) name) *slot*)
  ch)

(defn- proxy-unsubscribe [self name]
  (if-let [slot (get (self :subscriptions) name)]
    (do (cancel slot) (set ((self :subscriptions) name) nil))
    (errorf "unknown signal name: %s" name)))

(defn- proxy-members [members]
  (tabseq [[name member] :pairs members]
    name (case (member :kind)
           'method (proxy-method (string name) member)
           'property (proxy-property (string name) member)
           nil)))

(defn proxy
  ```
  Create a proxy object for an interface.

  `spec` must be the result of a call to `sdbus/introspect`, and
  `interface` must be a keyword in `(spec :interfaces)`.

  Returns a table exposing interface members according to the
  following:

  - **methods**: callable as variadic object methods in the form of
    `(:<method-name> <proxy-object> & args)`. D-Bus signatures are
    automatically handled based on introspection data, optional
    rest arguments are passed directly as arguments to the underlying
    D-Bus method. Returns the contents of the method call.

  - **properties**: callable as object methods in the form of
    `(:<property-name> <proxy-object> &opt value)`. When `value` is
    nil, get the current value of the property. Otherwise, set the
    property where `value` is a variant encoded as a Janet tuple.

  - **signals**: subscribe via `(:subscribe <proxy-object
    :<signal-name> &opt ch)`. When `ch` is a nil, a new channel is
    created. Returns the channel.  Unsubscribe via `(:unsubscribe
    <proxy-object> :<signal-name>)`.  PropertiesChanged signals for
    the specific interface are also provided as a signal-name.

  ```
  [bus spec interface]
  (def obj @{:bus bus
             :destination (spec :destination)
             :path (spec :path)
             :interface (string interface)
             :subscriptions @{}
             :subscribe proxy-subscribe
             :unsubscribe proxy-unsubscribe})
  (when (not (in (spec :interfaces) interface))
    (errorf "interface %s not found in spec" interface))
  (->> (get-in spec [:interfaces interface :members])
       (proxy-members)
       (merge-into obj)))

(defn- normalized-read [msg]
  (match (message-read-all msg)
    nil []
    [& rest] rest
    x [x]))

(defn- property-wrapper [fun]
  (fn [self msg]
    (try (fun self msg) ([err] err))))

(defn- property-getter [self reply]
  (message-append reply (self :sig) (self :value)))

(defn- property-setter [self msg]
  (def value (message-read-all msg))
  (when (deep-not= (self :value) value)
    (set (self :value) value)))

(defn property
  ```
  Create a D-Bus property definition map that may be passed as a
  struct/table member to `sdbus/export`.

  Accepts the following flags:

  * `:d` - Annotate the property as deprecated.

  * `:h` - Hide the property in introspection data.

  * `:r` - Mark the property as constant. Prevents emitting
    PropertiesChanged signals for the property.

  * `:e` - Emit a PropertiesChanged signal when the underlying property
    value changes.

  * `:i` - Emit a PropertiesChanged signal when the underlying property
    value changes, but without the value contents in the signal
    message.

  * `:x` - Annotate the property as requiring an explicit request for
    the property to be shown. Cannot be combined with `:e`. See the
    sd-bus documentation for SD_BUS_VTABLE_PROPERTY_EXPLICIT for more
    information.

  * `:w` - Set the property as writable.

  ```
  [signature value &opt flags]
  (default flags "")
  (when (not (string/check-set :dhreixw flags))
    (errorf "Invalid property flags: %s" flags))
  @{:type 'property
    :flags flags
    :writable (string/check-set flags :w)
    :sig signature
    :value value
    :getter (property-wrapper property-getter)
    :setter (property-wrapper property-setter)})

(defn- send-error [call err]
  (def error-msg (message-new-method-error call "org.janet.error" err))
  (message-send error-msg))

(defn- method-wrapper [fun out-signature]
  (fn [bus msg]
    (setdyn :sdbus/bus bus)
    (setdyn :sdbus/path (message-get-path msg))
    (setdyn :sdbus/interface (message-get-interface msg))
    (try (do
           (def reply (message-new-method-return msg))
           (def result (fun ;(normalized-read msg)))
           (if-not (empty? out-signature)
             (message-append reply out-signature result))
           (message-send reply))
      ([err fiber] (send-error msg err) (propagate err fiber)))))

(defn method
  ```
  Create a D-Bus method definition map that may be passed as a
  struct/table member to `sdbus/export`.

  Accepts the following flags:

  - `:d` - Annotate the method as deprecated.

  - `:h` - Hide the method in introspection data.

  - `:s` - Mark the method as sensitive, see
    `sd_bus_message_sensitive(3)` and SD_BUS_VTABLE_SENSITIVE for more
    information.

  - `:n` - Mark the method as not returning a reply.
  ```
  [in-signature out-signature fun &opt flags]
  (default in-signature "")
  (default out-signature "")
  (default flags "")
  (when (not (string/check-set ":dhsn" flags))
    (errorf "Invalid method flags: %s" flags))
  {:type 'method
   :flags flags
   :sig-in in-signature
   :sig-out out-signature
   :function (method-wrapper fun out-signature)})

(defn signal
  ```
  Create a D-Bus signal definition map that may be passed as a
  struct/table member to `sdbus/export`.

  Accepts the following flags:

  - `:d` - Annotate the signal as deprecated.

  - `:h` - Hide the signal in introspection data.
  ```
  [signature &opt flags]
  (default flags "")
  (when (not (string/check-set ":dh" flags))
    (errorf "Invalid signal flags: %s" flags))
  {:type 'signal
   :flags flags
   :sig signature})

(def- dbus-interface ["org.freedesktop.DBus"
                      "/org/freedesktop/DBus"
                      "org.freedesktop.DBus"])

(defn request-name
  ```
  Request ownership of a D-Bus name from the bus. Returns nil upon
  success, or :queued if placed in the ownership queue for
  `name`. Raises an error upon failure.

  Accepts the following flags:

  - `:a` - If the caller succeeds in acquiring ownership of the name,
    allows another D-Bus peer with the
    `DBUS_NAME_FLAG_REPLACE_EXISTING` flag set to replace the caller
    as owner. Corresponds to the `DBUS_NAME_FLAG_ALLOW_REPLACEMENT`
    flag in the D-Bus specification.

  - `:n` - Do not place the caller in the ownership queue if `name` is
    already owned. Corresponds to the `DBUS_NAME_FLAG_DO_NOT_QUEUE`
    flag in the D-Bus specification.

  - `:r` - If `name` is already owned and the owner has specified
    `DBUS_NAME_FLAG_ALLOW_REPLACEMENT`, replace the current
    owner. Corresponds to the `DBUS_NAME_FLAG_REPLACE_EXISTING` flag
    in the D-Bus specification.

  ```
  [bus name &opt flags]
  (default flags "")
  (def dbus-flag (->> (pairs {:a 0x1 :r 0x2 :n 0x4})
                      (keep (fn [[key val]] (when (string/check-set flags key) val)))
                      (reduce bor 0)))

  (def return-code (call-method bus ;dbus-interface "RequestName" "su" name dbus-flag))
  (case return-code
    1 nil
    2 :queued
    3 (errorf "%s: already has an owner" name)
    4 nil
    (errorf "%s: unknown error")))

(defn release-name
  ```
  Release ownership of a D-Bus name. Returns nil upon success,
  otherwise raises an error.
  ```
  [bus name]
  (def return-code (call-method bus ;dbus-interface "ReleaseName" "s" name))
  (case return-code
    1 nil
    2 (errorf "%s: does not exist" name)
    3 (errorf "%s: caller is not the owner" name)
    (errorf "%s: unknown error")))
