# SPDX-License-Identifier: MIT
# Copyright (c) 2025 Joshua Krusell

(defn- attribute-struct [attributes]
  (struct ;attributes))

(defn- annotation-struct [tag attributes children]
  (unless (and (attributes :name) (attributes :value))
    (error "expected name and value attributes for annotation"))
  {(keyword (attributes :name))
   (attributes :value)})

(defn- arg-struct [tag attributes children]
  (unless (attributes :type)
    (error "expected type attribute for arg"))
  {:name (get attributes :name "")
   :type (attributes :type)
   :direction (get attributes :direction "")
   :annotations children})

(defn- method-struct [tag attributes children]
  (unless (attributes :name)
    (error "expected name attribute for method"))
  {(keyword (attributes :name))
   {:kind 'method
    :in (filter |(= ($ :direction) "in") children)
    :out (filter |(= ($ :direction) "out") children)
    :annotations (filter |(nil? (in $ :direction)) children)}})

(defn- signal-struct [tag attributes children]
  (unless (attributes :name)
    (error "expected name attribute for signal"))
  {(keyword (attributes :name))
   {:kind 'signal
    :args (filter |(in $ :type) children)
    :annotations (filter |(nil? (in $ :direction)) children)}})

(defn- property-struct [tag attributes children]
  (unless (and (attributes :name) (attributes :type) (attributes :access))
    (error "expected name, type, and access attributes for property"))
  {(keyword (attributes :name))
   {:kind 'property
    :type (attributes :type)
    :access (attributes :access)
    :annotations children}})

(defn- interface-struct [tag attributes children]
  (unless (attributes :name)
    (error "expected name attribute for interface"))
  (def member? (fn [child] (dictionary? (first (values child)))))
  (def groups (group-by member? children))
  {(keyword (attributes :name))
   {:members (merge ;(get groups true []))
    :annotations (get groups false)}})

(defn- node-struct [tag attributes children]
  (def interface? (fn [child] (not (get child :path))))
  (def groups (group-by interface? children))
  {:path (get attributes :name "")
   :interfaces (merge ;(get groups true []))
   :children (get groups false)})

(defn- to-struct [tag attributes &opt children]
  (default children @[])
  (def dispatch-fn
    (case tag
      "node" node-struct
      "interface" interface-struct
      "property" property-struct
      "method" method-struct
      "signal" signal-struct
      "arg" arg-struct
      "annotation" annotation-struct))
  (dispatch-fn tag attributes children))

(defn- attribute [keys]
  (if (empty? keys) ':s
    ~(* (/ (<- (+ ,;keys)) ,keyword) "=" `"` (<- (any (if-not `"` 1))) `"` )))

(defn- empty-tag [tag]
  ~(* "<" (<- ,tag) :attributes "/>" :s*))

(defn- open-tag [tag]
  ~(* "<" (<- ,tag) :attributes ">" :s*))

(defn- close-tag [tag]
  ~(* "</" ,tag ">" :s*))

(defn- tag [name &named children attrs]
  (default children [])
  (default attrs [])
  ~(unref
     {:attribute ,(attribute attrs)
      :attributes (/ (group (any (* :s+ :attribute))) ,attribute-struct)
      :main (/ (+ ,(empty-tag name)
                  (* ,(open-tag name) (group (any (+ ,;children))) ,(close-tag name))) ,to-struct)}))

(def- grammar ~{:main (* (? :doctype) :node -1)
                :doctype (* "<!DOCTYPE" (thru ">") :s*)
                :node ,(tag "node" :children [:node :interface] :attrs ["name"])
                :interface ,(tag "interface"
                                 :children [:method :signal :property :annotation]
                                 :attrs ["name"])
                :method ,(tag "method" :children [:arg :annotation] :attrs ["name"])
                :signal ,(tag "signal" :children [:arg :annotation] :attrs ["name"])
                :property ,(tag "property" :children [:annotation]
                                :attrs ["name" "type" "access"])
                :arg ,(tag "arg" :attrs ["name" "type" "direction"])
                :annotation ,(tag "annotation" :attrs ["name" "value"])})

(def- compiled-grammar (peg/compile grammar))

(defn parse-xml [input &named destination path]
  "Parse D-Bus introspection data. Returns the parsed xml as a struct,
   or nil upon failure."
  (default destination "")
  (default path "/")
  (when-let [match (-> (peg/match compiled-grammar input) first)]
    {:destination destination
     :path path
     :interfaces (get match :interfaces)
     :children (get match :children)}))
