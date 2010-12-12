<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="doc">
    <top>
      <one>test</one>
      <two>
        <xsl:value-of select="concat(&quot;The answer is &quot;, results/answer, &quot;.&quot;)"/>
      </two>
      <three>
        <xsl:value-of select="concat(results/count, &quot; attempts made by &quot;, results/user)"/>
      </three>
      <four>
        <xsl:value-of select="concat(results/count, &quot; attempts made by &quot;, results/user)"/>
      </four>
      <five>
        <xsl:value-of select="results/count"/>
        <xsl:text> attempts made by </xsl:text>
        <xsl:value-of select="results/user"/>
      </five>
      <six>
        <xsl:value-of select="results/message"/>
      </six>
    </top>
  </xsl:template>
</xsl:stylesheet>
