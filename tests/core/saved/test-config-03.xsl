<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="configuration">
    <xsl:variable name="domain" select="domain-name"/>
    <xsl:apply-templates select="system/host-name">
      <xsl:with-param name="message" select="&quot;Invalid host-name&quot;"/>
      <xsl:with-param name="domain" select="$domain"/>
    </xsl:apply-templates>
  </xsl:template>
  <xsl:template match="host-name">
    <xsl:param name="message" select="&quot;Error&quot;"/>
    <xsl:param name="domain"/>
    <hello>
      <xsl:value-of select="concat($message, &quot;:: &quot;, ., &quot; (&quot;, $domain, &quot;)&quot;)"/>
    </hello>
  </xsl:template>
</xsl:stylesheet>
