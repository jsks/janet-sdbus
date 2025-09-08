# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./base :prefix "" :export true)
(import ./native :prefix "" :export true)
(import ./introspect :prefix "" :export true)
(import ./proxy :prefix "" :export true)

(defn introspect
  ```
  Get introspection data for a D-Bus object. Returns a struct.
  ```
  [bus destination path]
  (-> (call-method bus destination path "org.freedesktop.DBus.Introspectable" "Introspect")
      (parse-xml :destination destination :path path)))

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

(defn- property-getter [self reply]
  (message-append reply (self :sig) (self :value)))

(defn- property-setter [self msg]
  (def value (message-read-all msg))
  (when (deep-not= (self :value) value)
    (set (self :value) value)))

(defn property [signature value &opt flags]
  ```
  Create a D-Bus property definition map.
  ```
  (default flags "")
  (when (not (string/check-set :dhsreixw flags))
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
  (fn [msg]
    (try (do
           (def reply (message-new-method-return msg))
           (def result (fun ;(normalized-read msg)))
           (if-not (nil? result)
             (message-append reply out-signature result))
           (message-send reply))
      ([err fiber] (send-error msg err) (propagate err fiber)))))

(defn method [in-signature out-signature fun &opt flags]
  ```
  Create a D-Bus method definition map.
  ```
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
    Create a D-Bus signal definition map.
    ```
  [signature &opt flags]
  (default flags "")
  (when (not (string/check-set ":dhs" flags))
    (errorf "Invalid signal flags: %s" flags))
  {:type 'signal
   :flags flags
   :sig signature})

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
