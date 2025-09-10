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
  [bus member chan &named sender path interface argN]
  (default argN [])
  (def rules (symbolic-kvs interface member path sender))
  (def arg-matches (map (fn [[k v]] (string/format "arg%d='%s'" k v)) (pairs argN)))
  (def match-rule (-> (array/concat rules arg-matches)
                      (string/join ",")))
  (match-signal bus match-rule chan))

(defn subscribe-properties-changed [bus interface chan &named sender path]
  (subscribe-signal bus "PropertiesChanged" chan
                    :interface "org.freedesktop.DBus.Properties"
                    :sender sender
                    :path path
                    :argN [interface]))

(defn introspect
  ```
  Get introspection data for a D-Bus object. Returns a struct.
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

(defn proxy [bus spec interface]
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
