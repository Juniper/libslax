<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <op-script-results>
      <xsl:variable name="version-info-temp-1">
        <software-information>
          <host-name>test</host-name>
          <product-model>m7i</product-model>
          <product-name>m7i</product-name>
        </software-information>
      </xsl:variable>
      <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="version-info" select="slax-ext:node-set($version-info-temp-1)"/>
      <xsl:apply-templates select="$version-info/host-name"/>
    </op-script-results>
  </xsl:template>
  <xsl:template match="host-name" priority="-4">
    <output>Yellow</output>
  </xsl:template>
  <xsl:template match="host-name" priority="-3">
    <output>Blue</output>
  </xsl:template>
</xsl:stylesheet>
