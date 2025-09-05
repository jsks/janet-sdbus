# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(import ./base :prefix "" :export false)

(defn- proxy-method [name method]
  (def sig (-> (map |(get $ :type) (get method :in)) (string/join)))
  (fn [self & args]
    (let [bus (self :bus)
          destination (self :destination)
          path (self :path)
          interface (self :interface)
          name (string name)]
      (call-method bus destination path interface name sig ;args))))

(defn- proxy-property [name property]
  (def sig (get property :type))
  (fn [self &opt value]
    (let [bus (self :bus)
          destination (self :destination)
          path (self :path)
          interface (self :interface)
          name (string name)]
      (if (nil? value)
        (get-property bus destination path interface name)
        (set-property bus destination path interface name [sig value])))))

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
             :interface (string interface)})
  (when (not (in (spec :interfaces) interface))
    (errorf "interface %s not found in spec" interface))
  (->> (get-in spec [:interfaces interface :members])
       (proxy-members)
       (merge-into obj)))
