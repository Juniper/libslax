<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <xsl:variable name="user" select="&quot;test&quot;"/>
    <xsl:variable name="date" select="&quot;now&quot;"/>
    <top>
      <xsl:comment>
        <xsl:value-of select="concat(&quot;Added by user &quot;, $user, &quot; on &quot;, $date)"/>
      </xsl:comment>
    </top>
  </xsl:template>
</xsl:stylesheet>
