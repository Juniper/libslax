<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:output indent="yes"/>
  <xsl:variable name="slax-count" mvarname="count"/>
  <xsl:variable name="count" select="10" mutable="yes" svarname="slax-count"/>
  <xsl:template match="/">
    <top>
      <slax:while xmlns:slax="http://xml.libslax.org/slax" test="$count &gt; 0">
        <count>
          <xsl:value-of select="$count"/>
        </count>
        <slax:set-variable xmlns:slax="http://xml.libslax.org/slax" name="count" svarname="slax-count" select="$count - 1"/>
      </slax:while>
      <boom/>
    </top>
  </xsl:template>
</xsl:stylesheet>
