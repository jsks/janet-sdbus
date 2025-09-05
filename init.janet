# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./native :prefix "" :export true)
(import ./introspect :prefix "" :export true)

(defn call-method
  ```
  Send a method call to a D-Bus service. Suspends the current fiber
  without blocking the event loop.

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
      result (errorf "Unexpected result: %p" result))))

(defn introspect
  ```
  Get introspection data for a D-Bus object. Returns a struct.
  ```
  [bus destination path]
  (-> (call-method bus destination path "org.freedesktop.DBus.Introspectable" "Introspect")
      (parse-xml :destination destination :path path)))

(defn get-property
  ```
  Get a property from a D-Bus service.
  ```
  [bus destination path interface property]
  (call-method bus destination path
               "org.freedesktop.DBus.Properties" "Get"
               "ss" interface property))

(defn set-property
  ```
  Set a property on a D-Bus service.

  `variant` must a D-Bus variant encoded as a Janet tuple with two
  members: a valid D-Bus signature string and a corresponding value.
  ```
  [bus destination path interface property variant]
  (call-method bus destination path "org.freedesktop.DBus.Properties" "Set"
               "ssv" interface property variant))

(defn remote-method
  `Returns a function to call a D-Bus method.`
  [bus destination path interface method &opt signature]
  (partial call-method bus destination path interface method signature))

(defn remote-property
  ```
  Returns a function to get or set a D-Bus property.

  The returned function when called without arguments retrieves the
  current value of `property`.  When called with a variant, it sets
  the property to that value.
  ```
  [bus destination path interface property]
  (fn [&opt variant]
    (if (nil? variant)
      (get-property bus destination path interface property)
      (set-property bus destination path interface property variant))))

(defn- normalized-read [msg]
  (match (message-read-all msg)
    nil []
    [& rest] rest
    x [x]))

(defn- property-wrapper [fun]
  (fn [self msg]
    (try (fun self msg) ([err] err))))

(defn create-property [value &named sig flags]
  ```
  Create a D-Bus property definition map.
  ```
  (default flags "")
  (when (not (string/check-set :dhsreixw flags))
    (errorf "Invalid property flags: %s" flags))
  {:type 'property
   :flags flags
   :writable (string/check-set flags :w)
   :sig sig
   :value value
   :getter (property-wrapper (fn [self reply] (message-append reply (self :sig) (self :value))))
   :setter (property-wrapper (fn [self msg] (set (self :value) (normalized-read msg))))})

(defn- send-error [call err]
  (def error-msg (message-new-method-error call "org.janet.error" err))
  (message-send error-msg))

(defn- method-wrapper [fun out-signature]
  (fn [msg]
    (try (do
           (def reply (message-new-method-return msg))
           (def result (fun ;(normalized-read msg)))
           (if-not (nil? result)
             (message-append reply out-signature result))
           (message-send reply))
      ([err fiber] (send-error msg err) (propagate err fiber)))))

(defn create-method [fun &named in-sig out-sig flags]
  ```
  Create a D-Bus method definition map.
  ```
  (default in-sig "")
  (default out-sig "")
  (default flags "")
  (when (not (string/check-set ":dhsn" flags))
    (errorf "Invalid method flags: %s" flags))
  {:type 'method
   :flags flags
   :sig-in in-sig
   :sig-out out-sig
   :function (method-wrapper fun out-sig)})

(defn create-signal
  ```
    Create a D-Bus signal definition map.
    ```
  [signature &named sig flags]
  (default flags "")
  (when (not (string/check-set ":dhs" flags))
    (errorf "Invalid signal flags: %s" flags))
  {:type 'signal
   :sig signature})

(defmacro method [& forms]
  (match forms
    [[& args] & body]
      ~(sdbus/create-method (fn ,args ,;body))
    [flags [& args] & body]
      ~(sdbus/create-method (fn ,args ,;body) :flags ,flags)
    [':: in '-> out [& args] & body]
      ~(sdbus/create-method (fn ,args ,;body) :in-sig ,in :out-sig ,out)
    [':: in '-> out flags [& args] & body]
      ~(sdbus/create-method (fn ,args ,;body) :in-sig ,in :out-sig ,out :flags ,flags)
    ~(error "Invalid method definition syntax")))

(def- dbus-interface ["org.freedesktop.DBus"
                      "/org/freedesktop/DBus"
                      "org.freedesktop.DBus"])

(defn request-name
  ```
  * `:a` --- DBUS_NAME_FLAG_ALLOW_REPLACEMENT
  * `:n` --- DBUS_NAME_FLAG_DO_NOT_QUEUE
  * `:r` --- DBUS_NAME_FLAG_REPLACE_EXISTING
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
  [bus name]
  (def return-code (call-method bus ;dbus-interface "ReleaseName" "s" name))
  (case return-code
    1 nil
    2 (errorf "%s: does not exist" name)
    3 (errorf "%s: caller is not the owner" name)
    (errorf "%s: unknown error")))
