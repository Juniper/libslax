<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:xutil="http://xml.libslax.org/xutil" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="xutil slax">
  <xsl:template match="/">
    <op-script-results>
      <xsl:variable name="xml">
        <json>
          <color>red</color>
        </json>
      </xsl:variable>
      <xsl:variable name="str" select="xutil:xml-to-json($xml)"/>
      <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:output($str)"/>
    </op-script-results>
  </xsl:template>
</xsl:stylesheet>
