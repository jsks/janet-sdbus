# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./base :prefix "" :export false)
(import ./native :prefix "" :export false)

(defn- second [x] (get x 1))

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
        (-> (get-property bus destination path interface name) second)
        (set-property bus destination path interface name [sig value])))))

(defn- prop-changed-rule [path interface]
  (string/format "interface='%s',member='%s',path='%s',arg0='%s'"
                 "org.freedesktop.DBus.Properties"
                 "PropertiesChanged"
                 path
                 interface))

(defn- subscribe [self name &opt ch]
  (default ch (ev/chan))
  (var *slot* nil)
  (let [bus (self :bus)
        interface (self :interface)
        path (self :path)
        name (string name)]
    (if (= name "PropertiesChanged")
      (set *slot* (match-signal bus (prop-changed-rule path interface) ch))
      (set *slot* (subscribe-signal bus interface name ch :path path))))
  (set ((self :subscriptions) name) *slot*)
  ch)

(defn- unsubscribe [self name]
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
             :subscribe subscribe
             :unsubscribe unsubscribe})
  (when (not (in (spec :interfaces) interface))
    (errorf "interface %s not found in spec" interface))
  (->> (get-in spec [:interfaces interface :members])
       (proxy-members)
       (merge-into obj)))
