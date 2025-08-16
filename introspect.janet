(defn- to-struct [tag attr &opt children]
  (default children @[])
  {:tag tag
   :attributes (struct ;attr)
   :children children})

(def- grammar ~{:main (* (? :doctype) :root -1)
                :doctype (* "<!DOCTYPE" (thru ">") :s*)
                :root (* "<node>" :s* (any (* :elem :s*)) "</node>" :s*)
                :elem (+ :empty-tag :tag)
                :attr (* (/ (<- :w+) ,keyword) "=" `"` (<- (any (if-not `"` 1))) `"`)
                :attributes (group (any (* :s+ :attr)))
                :empty-tag (/ (* "<" (<- :w+) :attributes "/>") ,to-struct)
                :tag (unref
                       {:main (/ (* :open-tag :s* (group (any (+ :elem :s*))) :close-tag) ,to-struct)
                        :open-tag (* "<" (<- :w+ :tag-name) :attributes ">")
                        :close-tag (* "</" (backmatch :tag-name) ">")})})

(def- compiled-grammar (peg/compile grammar))

(defn parse-xml [input]
  "Parse D-Bus introspection data. Returns the parsed xml as a struct,
   or nil upon failure."
  (peg/match compiled-grammar input))
