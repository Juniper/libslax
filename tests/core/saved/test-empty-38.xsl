<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:param name="host" select="&quot;localhost&quot;"/>
  <xsl:template match="/">
    <data>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="x" select="slax:get-host($host)"/>
      <localhost>
        <xsl:copy-of select="$x"/>
      </localhost>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="y" select="slax:get-host($x/address[1])"/>
      <one-twenty-seven>
        <xsl:copy-of select="$y"/>
      </one-twenty-seven>
    </data>
  </xsl:template>
</xsl:stylesheet>
