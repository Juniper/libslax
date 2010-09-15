<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:param name="fido"/>
  <xsl:variable name="bone"/>
  <xsl:template match="/">
    <top>
      <xsl:variable name="dot" select="."/>
      <xsl:variable name="location" select="$dot/@location"/>
      <xsl:variable name="message" select="concat(&quot;We are in &quot;, $location, &quot; now.&quot;)"/>
      <fido>
        <xsl:value-of select="$fido"/>
      </fido>
    </top>
  </xsl:template>
</xsl:stylesheet>
