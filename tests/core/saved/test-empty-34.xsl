<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax-ext slax">
  <xsl:template match="/">
    <op-script-results>
      <xsl:variable name="xml-temp-1">
        <config>
          <system>
            <hostname>abc</hostname>
          </system>
        </config>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="xml" select="slax-ext:node-set($xml-temp-1)"/>
      <xsl:choose>
        <xsl:when test="$xml/config/system/hostname = &quot;abc&quot;">
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:output(&quot;work&quot;)"/>
        </xsl:when>
        <xsl:otherwise>
          <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:output(&quot;non-work&quot;)"/>
        </xsl:otherwise>
      </xsl:choose>
    </op-script-results>
  </xsl:template>
</xsl:stylesheet>
