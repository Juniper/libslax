<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:variable name="v1" select="&quot;before{during}after&quot;"/>
  <xsl:variable name="v2" select="&quot;two&quot;"/>
  <xsl:variable name="e1">
    <e1 a1="{$v1}-{{x}}-{$v2}"/>
  </xsl:variable>
  <xsl:variable name="e2">
    <e2 a1="{substring-before($v1, &quot;{&quot;)}-{$v2}"/>
  </xsl:variable>
  <xsl:template match="/">
    <top>
      <xsl:copy-of select="$e1"/>
      <xsl:copy-of select="$e2"/>
    </top>
  </xsl:template>
</xsl:stylesheet>
