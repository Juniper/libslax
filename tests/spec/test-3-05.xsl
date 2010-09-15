<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-trace="http://code.google.com/p/libslax/trace" version="1.0" extension-element-prefixes="slax-trace">
  <xsl:template match="/">
    <out>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="&quot;this&quot;"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace" select="that"/>
      <slax-trace:trace xmlns:slax-trace="http://code.google.com/p/libslax/trace">
        <some>thing</some>
      </slax-trace:trace>
    </out>
  </xsl:template>
</xsl:stylesheet>
