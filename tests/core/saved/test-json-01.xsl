<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:xutil="http://xml.libslax.org/xutil" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" version="1.0" extension-element-prefixes="xutil slax-ext">
  <xsl:variable name="tests-temp-1">
    <one>{ a: 1, b: 2.5e-5 }</one>
    <two>[ 1, 2, 3, 4 ]</two>
    <three>{ a: 1, b: [ 2, 3 ] }</three>
    <four>[ {a: 1}, {b: 2}, {c: 3} ]</four>
    <five>{ a: "fish", b: "tur key", c: "mon key suit" }</five>
    <six>
      <xsl:text>   {
      "Image": {
          "Width":  800,
          "Height": 600,
          "Title":  "View from 15th Floor",
          "Thumbnail": {
              "Url":    "http://www.example.com/image/481989943",
              "Height": 125,
              "Width":  "100"
          },
          "IDs": [116, 943, 234, 38793]
        }
     }</xsl:text>
    </six>
    <seven>
   [
      {
         "precision": "zip",
         "Latitude":  37.7668,
         "Longitude": -122.3959,
         "Address":   "",
         "City":      "SAN FRANCISCO",
         "State":     "CA",
         "Zip":       "94107",
         "Country":   "US"
      },
      {
         "precision": "zip",
         "Latitude":  37.371991,
         "Longitude": -122.026020,
         "Address":   "",
         "City":      "SUNNYVALE",
         "State":     "CA",
         "Zip":       "94085",
         "Country":   "US"
      }
   ]
</seven>
    <eight>[ { a: "nrt
&#13;	nrt" } ]</eight>
    <nine>{ "name": "Skip Tracer",
              "location": "The city that never sleeps",
              "age": 5,
              "real": false,
              "cases": null,
              "equipment": [ "hat", "desk", "attitude" ]
            }</nine>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="tests" select="slax-ext:node-set($tests-temp-1)"/>
  <xsl:param name="root" select="&quot;my-top&quot;"/>
  <xsl:param name="types"/>
  <xsl:param name="pretty"/>
  <xsl:variable name="j2x-opts">
    <xsl:if test="$root">
      <root>
        <xsl:value-of select="$root"/>
      </root>
    </xsl:if>
    <xsl:if test="$types">
      <types>
        <xsl:value-of select="$types"/>
      </types>
    </xsl:if>
  </xsl:variable>
  <xsl:variable name="x2j-opts">
    <xsl:if test="$pretty">
      <pretty/>
    </xsl:if>
  </xsl:variable>
  <xsl:template match="/">
    <top>
      <xsl:for-each select="$tests/node()">
        <xsl:element name="{name()}">
          <input>
            <xsl:value-of select="."/>
          </input>
          <xsl:variable name="x" select="xutil:json-to-xml(., $j2x-opts)"/>
          <xml>
            <xsl:copy-of select="$x"/>
          </xml>
          <back>
            <xsl:value-of select="xutil:xml-to-json($x, $x2j-opts)"/>
          </back>
        </xsl:element>
      </xsl:for-each>
    </top>
  </xsl:template>
</xsl:stylesheet>
