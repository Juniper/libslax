<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="content" select="&quot;&#10;line one&#10;line two&#10;line three&#10;line four&#10;line five&#10;se-ix&#10;se-ven&#10;eh-it&#10;ny-en&#10;te-en&#10;el-e-ven&#10;twiel-ve&#10;t'ir-teeen&#10;four-teen&#10;fift-teen&#10;se-ix-teen&#10;&quot;"/>
  <xsl:variable name="content2">
    <this>
      <xsl:value-of select="$content"/>
    </this>
  </xsl:variable>
  <xsl:template match="/">
    <top>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="lines" select="slax:break-lines($content)"/>
      <xsl:message>
        <xsl:value-of select="concat(&quot;done: &quot;, count($lines))"/>
      </xsl:message>
      <xsl:variable name="slax-dot-1" select="."/>
      <xsl:for-each select="$lines">
        <xsl:variable name="line" select="."/>
        <xsl:for-each select="$slax-dot-1">
          <xsl:copy-of select="$line"/>
        </xsl:for-each>
      </xsl:for-each>
      <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="lines2" select="slax:break-lines($content2)"/>
      <xsl:message>
        <xsl:value-of select="concat(&quot;done: &quot;, count($lines))"/>
      </xsl:message>
      <xsl:variable name="slax-dot-2" select="."/>
      <xsl:for-each select="$lines2">
        <xsl:variable name="line2" select="."/>
        <xsl:for-each select="$slax-dot-2">
          <xsl:copy-of select="$line2"/>
        </xsl:for-each>
      </xsl:for-each>
    </top>
  </xsl:template>
</xsl:stylesheet>
